// Cache das respostas que gastam IA (perguntas livres, itens contra, pesquisa na web).
//
// Cada entrada fica num "espaço" (counter, pergunta, openspeak, web) com uma chave. A busca
// tenta a chave exata e, se a entrada tiver as palavras da pergunta, também uma pergunta
// parecida: mesmas palavras importantes (sem acento, sem palavras de ligação), com pelo menos
// SIMILARITY de semelhança (Jaccard) e dentro do mesmo `scope` (ex.: o mesmo herói).
// Fica salvo num arquivo JSON, então sobrevive a reinícios e deploys.

import fs from 'node:fs';

/** Palavras que não mudam o sentido da pergunta (já sem acento). */
const STOPWORDS = new Set(('a o as os um uma uns umas de do da dos das pra pro pras pros para por que qual quais quem e eh '
  + 'no na nos nas num numa em com sem sobre me te se eu voce vc tu ele ela eles elas isso esse essa este esta aquele aquela '
  + 'como ai la tem ter ta tah esta estao sao ser foi vai mano cara frisquila oi ola hey fala salve ae opa por favor pf pfv '
  + 'mais muito muita bem so ja ou mas tambem tb entao sabe saber diz dizer me fala alguem alguma algum the is of to').split(' '));

/** Semelhança mínima entre os conjuntos de palavras para valer como "a mesma pergunta". */
export const SIMILARITY = 0.8;

/** Palavras importantes da pergunta, sem acento, sem repetir e em ordem (vira a chave). */
export function questionWords(text) {
  const words = String(text ?? '').normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase()
    .replace(/<@!?\d+>/g, ' ').replace(/https?:\/\/\S+/g, ' ').split(/[^a-z0-9&]+/)
    .filter(w => w && !STOPWORDS.has(w))
    .map(w => (w.length > 4 && w.endsWith('s') ? w.slice(0, -1) : w)); // "itens"/"item" não, mas "builds" -> "build"
  return [...new Set(words)].sort();
}

/** Jaccard entre dois conjuntos de palavras (0 a 1). */
export function similarity(a, b) {
  if (!a.length || !b.length) return 0;
  const sa = new Set(a);
  const inter = b.filter(w => sa.has(w)).length;
  return inter / (sa.size + new Set(b).size - inter);
}

export class AnswerCache {
  /**
   * @param {object} o
   * @param {string|null} o.file arquivo JSON (null = só na memória, para os testes)
   * @param {number} o.max entradas no total (as mais antigas saem primeiro)
   */
  constructor({ file = null, max = 500, now = () => Date.now(), log = () => {} } = {}) {
    this.file = file;
    this.max = max;
    this.now = now;
    this.log = log;
    this.entries = new Map(); // "espaço\u0000chave" -> {ns, key, at, ttl, scope, words, value}
    this.saveTimer = null;
    if (file) this.load();
  }

  load() {
    try {
      const data = JSON.parse(fs.readFileSync(this.file, 'utf8'));
      for (const e of Array.isArray(data?.entries) ? data.entries : []) {
        if (e && typeof e.ns === 'string' && typeof e.key === 'string' && this.now() - e.at < e.ttl) this.entries.set(`${e.ns}\u0000${e.key}`, e);
      }
    } catch (err) {
      if (err.code !== 'ENOENT') this.log('cache: não consegui ler', this.file, err.message);
    }
  }

  /** Grava no arquivo no máximo a cada 2 s (várias respostas seguidas viram uma gravação). */
  scheduleSave() {
    if (!this.file || this.saveTimer) return;
    this.saveTimer = setTimeout(() => {
      this.saveTimer = null;
      this.saveNow();
    }, 2000);
    this.saveTimer.unref?.();
  }

  saveNow() {
    if (!this.file) return;
    try {
      const tmp = `${this.file}.tmp`;
      fs.writeFileSync(tmp, JSON.stringify({ entries: [...this.entries.values()] }));
      fs.renameSync(tmp, this.file);
    } catch (err) {
      this.log('cache: não consegui gravar', this.file, err.message);
    }
  }

  fresh(e) {
    return e && this.now() - e.at < e.ttl;
  }

  /**
   * Valor guardado para a chave exata ou, com `words`, para uma pergunta parecida do mesmo
   * `scope`. Perguntas de até 2 palavras importantes só valem com a chave exata.
   */
  get(ns, key, { words = null, scope = null } = {}) {
    const exact = this.entries.get(`${ns}\u0000${key}`);
    if (this.fresh(exact) && (exact.scope ?? null) === (scope ?? null)) return exact.value;
    if (!words || words.length < 3) return null;
    let best = null;
    for (const e of this.entries.values()) {
      if (e.ns !== ns || (e.scope ?? null) !== (scope ?? null) || !e.words || !this.fresh(e)) continue;
      const s = similarity(words, e.words);
      if (s >= SIMILARITY && (!best || s > best.s)) best = { s, e };
    }
    return best ? best.e.value : null;
  }

  set(ns, key, value, { ttl, words = null, scope = null }) {
    const id = `${ns}\u0000${key}`;
    this.entries.delete(id); // volta para o fim (mais nova)
    this.entries.set(id, { ns, key, at: this.now(), ttl, scope: scope ?? null, words, value });
    for (const [k, e] of this.entries) if (!this.fresh(e)) this.entries.delete(k);
    while (this.entries.size > this.max) this.entries.delete(this.entries.keys().next().value);
    this.scheduleSave();
  }

  /** Quantas entradas válidas há em cada espaço (para o log). */
  stats() {
    const out = {};
    for (const e of this.entries.values()) if (this.fresh(e)) out[e.ns] = (out[e.ns] ?? 0) + 1;
    return out;
  }
}

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { AnswerCache, questionWords, similarity } from '../src/answercache.js';

test('palavras importantes: sem acento, sem palavras de ligação, sem menção', () => {
  assert.deepEqual(questionWords('<@123> Qual é a lore do Cultista??'), ['cultista', 'lore']);
  assert.deepEqual(questionWords('qual a história do cultista'), ['cultista', 'historia']);
  assert.deepEqual(questionWords('melhores builds pro Seven'), ['build', 'melhore', 'seven']);
  assert.equal(similarity(['a', 'b', 'c'], ['a', 'b', 'c']), 1);
  assert.equal(similarity(['a', 'b'], []), 0);
});

test('acha pergunta igual ou parecida, no mesmo escopo e dentro do prazo', () => {
  let t = 0;
  const cache = new AnswerCache({ now: () => t });
  const w1 = questionWords('como funciona o sistema de almas no mapa do deadlock');
  cache.set('pergunta', w1.join(' '), 'resposta', { ttl: 1000, words: w1 });
  // Mesmas palavras em outra ordem e com outras palavras de ligação: chave exata.
  assert.equal(cache.get('pergunta', questionWords('no deadlock, como funciona o sistema das almas no mapa?').join(' ')), 'resposta');
  // Parecida (uma palavra a mais): pela semelhança.
  const w2 = questionWords('como funciona o sistema de almas no mapa do deadlock exatamente');
  assert.equal(cache.get('pergunta', w2.join(' '), { words: w2 }), 'resposta');
  // Diferente demais, outro escopo ou outro espaço: não.
  const w3 = questionWords('como funciona a loja no mapa do deadlock');
  assert.equal(cache.get('pergunta', w3.join(' '), { words: w3 }), null);
  assert.equal(cache.get('pergunta', w1.join(' '), { words: w1, scope: '13' }), null);
  assert.equal(cache.get('openspeak', w1.join(' '), { words: w1 }), null);
  // Perguntas curtas só pela chave exata ("o viscous é casado" ≠ "o viscous é solteiro").
  const c1 = questionWords('o viscous é casado?');
  cache.set('pergunta', c1.join(' '), 'piada', { ttl: 1000, words: c1, scope: '7' });
  const c2 = questionWords('o viscous é solteiro?');
  assert.equal(cache.get('pergunta', c2.join(' '), { words: c2, scope: '7' }), null);
  assert.equal(cache.get('pergunta', c1.join(' '), { words: c1, scope: '7' }), 'piada');
  // Venceu o prazo: some.
  t = 1000;
  assert.equal(cache.get('pergunta', w1.join(' '), { words: w1 }), null);
});

test('limite de entradas e arquivo que sobrevive ao reinício', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'frisquila-cache-'));
  const file = path.join(dir, 'cache.json');
  const cache = new AnswerCache({ file, max: 2 });
  cache.set('web', 'a', 1, { ttl: 60_000 });
  cache.set('web', 'b', 2, { ttl: 60_000 });
  cache.set('web', 'c', { texto: 'x', fontes: [] }, { ttl: 60_000 });
  assert.equal(cache.get('web', 'a'), null); // a mais antiga saiu
  cache.saveNow();
  clearTimeout(cache.saveTimer);
  const again = new AnswerCache({ file });
  assert.deepEqual(again.get('web', 'c'), { texto: 'x', fontes: [] });
  assert.deepEqual(again.stats(), { web: 2 });
  assert.equal(new AnswerCache({ file: path.join(dir, 'nao-existe.json') }).stats().web, undefined);
  fs.rmSync(dir, { recursive: true, force: true });
});

test('resposta de itens contra vai para o cache só com ids e volta com os itens atuais', async () => {
  const { packCounterAnswer, unpackCounterAnswer } = await import('../src/ask.js');
  const metal = { id: 1, name: 'Metal Skin' }, knock = { id: 2, name: 'Knockdown' };
  const ult = { slot: 4, name: 'Death Slam' };
  const hero = { abilities: new Map([[9, ult]]) };
  const answer = { intro: 'oi', items: [{ item: metal, gain: 0.02, ability: null, motivo: 'm1' }, { item: knock, gain: null, ability: ult, motivo: 'm2' }],
    best: { item: knock, motivo: 'b' }, tip: '' };
  const packed = JSON.parse(JSON.stringify(packCounterAnswer(answer)));
  assert.deepEqual(packed.items[1], { id: 2, gain: null, slot: 4, motivo: 'm2' });
  const items = new Map([[1, metal], [2, knock]]);
  assert.deepEqual(unpackCounterAnswer(packed, hero, items), answer);
  assert.equal(unpackCounterAnswer(packed, hero, new Map([[1, metal]])), null); // item saiu do jogo
});

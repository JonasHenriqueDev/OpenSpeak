import { test } from 'node:test';
import assert from 'node:assert/strict';
import { askGateway, findHeroInText, gatewayMessages, parseGatewayAnswer, parseQuestion } from '../src/ask.js';
import { normalize } from '../src/deadlock.js';

const hero = (id, name, namePt = name, cls = name) => ({ id, name, namePt, keys: [normalize(name), normalize(namePt), normalize(cls)] });
const HEROES = [hero(2, 'Seven', 'Sete', 'gigawatt'), hero(13, 'Haze', 'Bruma'), hero(72, 'Billy'), hero(4, 'Lady Geist'),
  hero(18, 'Mo & Krill', 'Mo e Krill', 'krill'), hero(69, 'The Doorman', 'Porteiro', 'doorman')];

test('acha o herói citado, inclusive nome composto, português e apelido', () => {
  assert.equal(findHeroInText('<@123> melhor build pra seven', HEROES).name, 'Seven');
  assert.equal(findHeroInText('build da sete', HEROES).name, 'Seven');
  assert.equal(findHeroInText('como jogar de lady geist?', HEROES).name, 'Lady Geist');
  assert.equal(findHeroInText('quem countera o mo?', HEROES).name, 'Mo & Krill');
  assert.equal(findHeroInText('build pra seven contra haze', HEROES).name, 'Seven'); // o primeiro citado
  assert.equal(findHeroInText('bom dia pessoal', HEROES), null);
});

test('entende sozinho o tipo e o rank quando a pergunta é clara', () => {
  assert.deepEqual(pick(parseQuestion('melhor build pra seven', HEROES)), ['build', 'Seven', 'todos']);
  assert.deepEqual(pick(parseQuestion('quem countera o billy?', HEROES)), ['counter', 'Billy', 'todos']);
  assert.deepEqual(pick(parseQuestion('quem ganha do billy em oracle', HEROES)), ['counter', 'Billy', 'alto']);
  assert.deepEqual(pick(parseQuestion('haze tá forte em eternus?', HEROES)), ['heroi', 'Haze', 'topo']);
  assert.deepEqual(pick(parseQuestion('que itens comprar de bruma', HEROES)), ['build', 'Haze', 'todos']);
  assert.deepEqual(pick(parseQuestion('build pra seven contra haze', HEROES)), ['build', 'Seven', 'todos']);
  // Sem herói ou sem palavra-chave: deixa para a IA.
  assert.equal(parseQuestion('qual o melhor herói pra iniciante?', HEROES), null);
  assert.equal(parseQuestion('seven', HEROES), null);
});

function pick(q) {
  return q && [q.kind, q.hero.name, q.rank];
}

test('resposta da IA: JSON (mesmo com texto em volta) vira tipo, herói e rank válidos', () => {
  assert.deepEqual(parseGatewayAnswer('{"tipo":"counter","heroi":"Billy","rank":"alto"}'), { kind: 'counter', heroName: 'Billy', itemName: null, rank: 'alto' });
  assert.deepEqual(parseGatewayAnswer('Claro! {"tipo":"outro","heroi":null,"rank":"xyz"}'), { kind: null, heroName: null, itemName: null, rank: 'todos' });
  assert.throws(() => parseGatewayAnswer('não sei'));
  const msgs = gatewayMessages('qual o melhor herói?', HEROES);
  assert.equal(msgs[0].role, 'system');
  assert.match(msgs[0].content, /Seven \(Sete\)/);
});

test('cliente do gateway: resposta inválida pede de novo sem aquele provedor', async () => {
  const pedidos = [];
  const fetchImpl = async (url, opts) => {
    pedidos.push({ url, auth: opts.headers.authorization, body: JSON.parse(opts.body) });
    const texto = pedidos.length === 1 ? 'lixo' : '{"tipo":"build","heroi":"Seven","rank":"todos"}';
    return { ok: true, json: async () => ({ texto, provedor: pedidos.length === 1 ? 'groq' : 'gemini', modelo: 'm' }) };
  };
  const r = await askGateway({ url: 'http://127.0.0.1:8092/', token: 'iag_x', fetchImpl }, [{ role: 'user', content: 'oi' }]);
  assert.deepEqual(r, { kind: 'build', heroName: 'Seven', itemName: null, rank: 'todos' });
  assert.equal(pedidos[0].url, 'http://127.0.0.1:8092/v1/chat');
  assert.equal(pedidos[0].auth, 'Bearer iag_x');
  assert.equal(pedidos[0].body.tarefa, 'frisquila');
  assert.deepEqual(pedidos[1].body.excluir, ['groq']);
  assert.equal(pedidos[1].body.rejeitado.provedor, 'groq');
  await assert.rejects(askGateway({ url: 'http://x', token: 't', fetchImpl: async () => ({ ok: false, status: 503 }) }, []));
});

const ITEMS = [
  { id: 1, name: 'Toxic Bullets', namePt: 'Balas Tóxicas', shopable: true, slot: 'weapon', cost: 1600, desc: 'Sangramento.', keys: [normalize('Toxic Bullets'), normalize('Balas Tóxicas')] },
  { id: 2, name: 'Decay', namePt: 'Degeneração', shopable: true, slot: 'spirit', cost: 3200, desc: 'Anticura.', keys: [normalize('Decay'), normalize('Degeneração')] },
];

test('tipos novos: info, itens contra, ranking e item', () => {
  assert.deepEqual(pick(parseQuestion('que herói é o billy?', HEROES, ITEMS)), ['info', 'Billy', 'todos']);
  assert.deepEqual(pick(parseQuestion('qual a lore da lady geist', HEROES, ITEMS)), ['info', 'Lady Geist', 'todos']);
  assert.deepEqual(pick(parseQuestion('habilidades do seven', HEROES, ITEMS)), ['info', 'Seven', 'todos']);
  assert.deepEqual(pick(parseQuestion('ordem de habilidades do seven', HEROES, ITEMS)), ['build', 'Seven', 'todos']);
  assert.deepEqual(pick(parseQuestion('melhores itens pra counterar o haze', HEROES, ITEMS)), ['itens_contra', 'Haze', 'todos']);
  assert.deepEqual(pick(parseQuestion('que item comprar contra billy em eternus', HEROES, ITEMS)), ['itens_contra', 'Billy', 'topo']);
  const ranking = parseQuestion('melhores heróis agora em oracle', HEROES, ITEMS);
  assert.deepEqual([ranking.kind, ranking.rank], ['ranking', 'alto']);
  assert.equal(parseQuestion('tier list atual', HEROES, ITEMS).kind, 'ranking');
  const item = parseQuestion('o que faz o toxic bullets?', HEROES, ITEMS);
  assert.deepEqual([item.kind, item.item.name], ['item', 'Toxic Bullets']);
  assert.equal(parseQuestion('balas tóxicas', HEROES, ITEMS).item.name, 'Toxic Bullets');
  assert.equal(parseQuestion('como funciona a urna?', HEROES, ITEMS), null); // aberta: vai para a IA
});

test('contexto oficial para a IA: lore, habilidades e item citados', async () => {
  const { officialContext, answerMessages } = await import('../src/ask.js');
  const billy = { ...HEROES.find(h => h.name === 'Billy'), lore: 'Bodeficação da raiva.', role: 'brawler', tags: ['Punk'],
    abilities: new Map([[9, { slot: 1, name: 'Bashdown', namePt: 'Tacada', desc: 'Bate o taco.' }]]) };
  const ctx = officialContext('o decay é bom contra o billy?', [billy, ...HEROES.filter(h => h.name !== 'Billy')], ITEMS);
  assert.match(ctx, /Bodeficação/);
  assert.match(ctx, /Tacada \(Bashdown\): Bate o taco/);
  assert.match(ctx, /ITEM Decay \(Degeneração\).*Anticura/);
  const msgs = answerMessages('pergunta?', ctx);
  assert.match(msgs[1].content, /^CONTEXTO:/);
  assert.match(msgs[0].content, /nunca invente/);
  assert.match(msgs[0].content, /FATOS:\n.*Valve/);
});

test('marcas das habilidades e itens pelo texto oficial', async () => {
  const { abilityTags, itemTags } = await import('../src/ask.js');
  assert.deepEqual(abilityTags('Canaliza uma nuvem tempestuosa, causando dano.'), ['CANALIZADA', 'DANO']);
  assert.deepEqual(abilityTags('Causa dano espiritual e aplica um congelamento temporal.'), ['CONTROLE', 'DANO']);
  assert.deepEqual(itemTags('Aplica um atordoamento após 2s.'), ['ATORDOA']);
  assert.deepEqual(itemTags('Ganha uma barreira quando for atordoado, acorrentado ou silenciado.'), ['CONTRA_CONTROLE', 'DEFESA_DE_DANO']);
  assert.deepEqual(itemTags('Reduz o efeito de desacelerações de movimento causadas pelos inimigos.'), ['CONTRA_LENTIDÃO']);
  assert.deepEqual(itemTags('O seu dano espiritual reduz as curas recebidas por inimigos.'), ['ANTICURA']);
});

test('"Por quê?" dos itens contra: só itens da lista, ligados a habilidades do herói', async () => {
  const { counterCandidates, counterReasonMessages, parseCounterReasons, counterReasonGateway } = await import('../src/ask.js');
  const { renderCounterReasons, renderCounterItems } = await import('../src/dlview.js');
  const it = (id, name, namePt, desc, tier = 1) => ({ id, name, namePt, desc, tier, slot: 'vitality', shopable: true });
  const knockdown = it(1, 'Knockdown', 'Nocaute', 'Aplica um atordoamento após 2s.', 3);
  const spellbreaker = it(2, 'Spellbreaker', 'Quebra-Magias', 'A próxima instância de dano espiritual alto sofrido é reduzida.', 4);
  const decay = it(3, 'Decay', 'Degeneração', 'Causa dano por segundo.', 3);
  const boring = it(4, 'Extra Health', 'Vida Extra', 'Mais vida.');
  const items = [knockdown, spellbreaker, decay, boring, { ...it(5, 'Velho', 'Velho', 'atordoamento'), shopable: false }];
  const list = [{ item: decay, gain: 0.03 }];
  const cand = counterCandidates(list, items);
  assert.deepEqual(cand.map(c => c.item.name), ['Decay', 'Knockdown', 'Spellbreaker']); // estatística + ferramentas, sem fora da loja
  assert.equal(cand[0].gain, 0.03);

  const hero = { id: 9, name: 'Seven', namePt: 'Seven', abilities: new Map([[4, { slot: 4, name: 'Storm Cloud', namePt: 'Nuvem Tempestuosa', desc: 'Canaliza uma nuvem.' }]]) };
  const msgs = counterReasonMessages(hero, cand);
  assert.match(msgs[1].content, /Nuvem Tempestuosa/);
  assert.match(msgs[1].content, /Nuvem Tempestuosa \(Storm Cloud\) \[CANALIZADA\]/);
  assert.match(msgs[1].content, /Knockdown \(Nocaute\) \[ATORDOA\]: Aplica um atordoamento/);
  assert.match(msgs[1].content, /Decay \(Degeneração\) \[\+3\.0 pp/);

  const answer = JSON.stringify({ itens: [
    { item: 'nocaute', habilidade: 'Storm Cloud', motivo: 'O atordoamento cancela a nuvem canalizada.' },
    { item: 'Inventado', habilidade: 'x', motivo: 'não existe' },
    { item: 'Spellbreaker', habilidade: 'Habilidade falsa', motivo: 'Reduz um golpe forte.' },
    { item: 'Knockdown', habilidade: 'Storm Cloud', motivo: 'repetido' },
  ] });
  const reasons = parseCounterReasons(`ok: ${answer}`, hero, cand);
  assert.deepEqual(reasons.map(r => r.item.name), ['Knockdown', 'Spellbreaker']);
  assert.equal(reasons[0].ability.namePt, 'Nuvem Tempestuosa');
  assert.equal(reasons[1].ability, null);
  assert.throws(() => parseCounterReasons('{"itens":[{"item":"Inventado","motivo":"x"}]}', hero, cand), /nenhum item/);
  // Anticura contra habilidade sem cura: descartada.
  const healbane = it(6, 'Healbane', 'Maldição Anticura', 'O seu dano espiritual reduz as curas recebidas por inimigos.');
  const withHb = [...cand, { item: healbane, gain: null }];
  assert.throws(() => parseCounterReasons(JSON.stringify({ itens: [{ item: 'Healbane', habilidade: 'Storm Cloud', motivo: 'corta cura' }] }), hero, withHb), /nenhum item/);

  // Resposta inválida: troca de provedor e tenta de novo.
  const calls = [];
  const fetchImpl = async (url, opts) => {
    const body = JSON.parse(opts.body);
    calls.push(body);
    return { ok: true, json: async () => ({ provedor: `p${calls.length}`, texto: calls.length === 1 ? 'sem json' : answer }) };
  };
  const got = await counterReasonGateway({ url: 'http://x', token: 't', fetchImpl }, hero, cand);
  assert.equal(got.length, 2);
  assert.equal(calls[0].tarefa, 'frisquila_resposta');
  assert.deepEqual(calls[1].excluir, ['p1']);

  const e = renderCounterReasons({ hero, reasons }).embeds[0].toJSON();
  assert.match(e.description, /\*\*Knockdown \(Nocaute\)\*\* → contra \*Nuvem Tempestuosa\*/);
  assert.match(e.footer.text, /IA/);
  const buttons = renderCounterItems({ hero, rank: 'alto', list }).components[0].toJSON().components;
  assert.equal(buttons[0].custom_id, 'dl:why:9:alto');
});

test('acha a habilidade citada: nome, "ult" ou número', async () => {
  const { findAbilityInText } = await import('../src/ask.js');
  const lash = { name: 'Lash', namePt: 'Lash', abilities: new Map([
    [1, { slot: 1, name: 'Ground Strike', namePt: 'Golpe no Chão' }],
    [2, { slot: 2, name: 'Grapple', namePt: 'Gancho' }],
    [4, { slot: 4, name: 'Death Slam', namePt: 'Pancada Mortal' }],
  ]) };
  assert.equal(findAbilityInText('item bom para counterar ult do lash', lash).slot, 4);
  assert.equal(findAbilityInText('o que comprar contra o death slam', lash).slot, 4);
  assert.equal(findAbilityInText('item contra o gancho do lash', lash).slot, 2);
  assert.equal(findAbilityInText('item contra a habilidade 1 do lash', lash).slot, 1);
  assert.equal(findAbilityInText('item bom contra lash', lash), null);
  assert.equal(findAbilityInText('item bom contra lash', { abilities: new Map() }), null);
});

test('"item bom contra X" em conversa: só itens da lista, com a escolha final', async () => {
  const { counterAnswerMessages, parseCounterAnswer, counterAnswerGateway } = await import('../src/ask.js');
  const { renderCounterAnswer } = await import('../src/dlview.js');
  const it = (id, name, namePt, desc) => ({ id, name, namePt, desc, tier: 2, slot: 'vitality', shopable: true });
  const unstoppable = it(1, 'Unstoppable', 'Imparável', 'Fica imune a atordoamentos, silêncios e malefícios de controle.');
  const metal = it(2, 'Metal Skin', 'Pele de Metal', 'Fica imune a balas por alguns segundos.');
  const healbane = it(3, 'Healbane', 'Maldição Anticura', 'O seu dano espiritual reduz as curas recebidas por inimigos.');
  const cand = [{ item: metal, gain: 0.021 }, { item: unstoppable, gain: null }, { item: healbane, gain: null }];
  const ult = { slot: 4, name: 'Death Slam', namePt: 'Pancada Mortal', desc: 'Agarra os inimigos, puxa e atordoa ao bater no chão.' };
  const lash = { id: 12, name: 'Lash', namePt: 'Lash', abilities: new Map([[4, ult]]) };

  const msgs = counterAnswerMessages('item contra ult do lash', lash, cand, { ability: ult });
  assert.match(msgs[0].content, /ESPECIFICAMENTE a habilidade 4 do herói, Pancada Mortal \(Death Slam\), a ult/);
  assert.match(msgs[1].content, /Metal Skin \(Pele de Metal\).*\+2\.1 pp/);
  assert.doesNotMatch(msgs[1].content, /WEB:/);
  assert.match(counterAnswerMessages('x', lash, cand, { web: 'reddit: compre unstoppable' })[1].content, /WEB:\nreddit/);

  const answer = JSON.stringify({
    intro: 'Contra a ult do Lash, vai de:',
    itens: [
      { item: 'Unstoppable', habilidade: null, motivo: 'Ative quando ele pular e ele não te pega.' },
      { item: 'Inventado', habilidade: null, motivo: 'não existe' },
      { item: 'Healbane', habilidade: null, motivo: 'corta cura' }, // ult sem cura: descartado
      { item: 'pele de metal', habilidade: 'Death Slam', motivo: 'Segura o dano das balas depois.' },
    ],
    escolha: { item: 'Unstoppable', motivo: 'É o counter mais direto.' },
    dica: 'null',
  });
  const got = parseCounterAnswer(answer, lash, cand, { ability: ult });
  assert.deepEqual(got.items.map(x => x.item.name), ['Unstoppable', 'Metal Skin']);
  assert.equal(got.items[0].ability, ult);
  assert.equal(got.best.item.name, 'Unstoppable');
  assert.equal(got.tip, '');
  // Escolha que não está entre os itens: fica o primeiro.
  assert.equal(parseCounterAnswer(answer.replace('"item":"Unstoppable","motivo":"É', '"item":"Nada","motivo":"É'), lash, cand).best.item.name, 'Unstoppable');
  assert.throws(() => parseCounterAnswer('{"itens":[{"item":"Unstoppable","motivo":"x"}]}', lash, cand), /de menos/);

  let n = 0;
  const fetchImpl = async () => ({ ok: true, json: async () => ({ provedor: `p${++n}`, texto: n === 1 ? '{}' : answer }) });
  const viaGateway = await counterAnswerGateway({ url: 'http://x', token: 't', fetchImpl }, 'q', lash, cand, { ability: ult });
  assert.equal(viaGateway.items.length, 2);

  const view = renderCounterAnswer({ question: 'item contra ult do lash', hero: lash, rank: 'todos', ability: ult, answer: got,
    sources: [{ title: 'Reddit', host: 'reddit.com', url: 'https://reddit.com/r/x' }] });
  const e = view.embeds[0].toJSON();
  assert.match(e.description, /^Contra a ult do Lash, vai de:/);
  assert.match(e.description, /🥇 \*\*Unstoppable\*\* \(Imparável\) — Ative/);
  assert.match(e.description, /🥈 \*\*Metal Skin\*\* \(Pele de Metal\) — .*\+2,1 pp/);
  assert.match(e.description, /Se eu tivesse que escolher só um: Unstoppable\*\* — É o counter mais direto\./);
  assert.match(e.description, /Fontes: \[Reddit\]\(<https:\/\/reddit\.com\/r\/x>\)/);
  assert.match(e.footer.text, /busca na web/);
  assert.equal(view.components[0].toJSON().components[0].custom_id, 'dl:citems:12:todos');
});

test('pesquisa na web pelo ia-gateway: texto, fontes e consultas', async () => {
  const { searchGateway, counterSearchQuery, openSearchQuery, answerMessages } = await import('../src/ask.js');
  const calls = [];
  const fetchImpl = async (url, opts) => {
    calls.push({ url, body: JSON.parse(opts.body), auth: opts.headers.authorization });
    return { ok: true, json: async () => ({ texto: ' Compre Unstoppable. ', fontes: [
      { titulo: 'reddit.com', url: 'https://vertexaisearch.cloud.google.com/a' }, { titulo: 'x', url: 'javascript:alert(1)' }] }) };
  };
  const got = await searchGateway({ url: 'http://gw/', token: 't', fetchImpl }, 'itens contra lash', { instrucao: 'breve' });
  assert.equal(calls[0].url, 'http://gw/v1/pesquisa');
  assert.equal(calls[0].auth, 'Bearer t');
  assert.deepEqual(calls[0].body, { consulta: 'itens contra lash', instrucao: 'breve', timeout_s: 20 });
  assert.equal(got.texto, 'Compre Unstoppable.');
  assert.deepEqual(got.fontes, [{ title: 'reddit.com', host: 'vertexaisearch.cloud.google.com', url: 'https://vertexaisearch.cloud.google.com/a' }]);
  await assert.rejects(searchGateway({ url: 'http://gw', token: 't', fetchImpl: async () => ({ ok: false, status: 503 }) }, 'x'), /503/);

  const lash = { name: 'Lash' };
  assert.match(counterSearchQuery(lash).consulta, /counterar o herói Lash\?/);
  assert.match(counterSearchQuery(lash, { name: 'Death Slam' }).consulta, /habilidade "Death Slam" do herói Lash/);
  assert.equal(openSearchQuery('<@1> o viscous é casado?').consulta, 'Deadlock (jogo da Valve): o viscous é casado?');
  assert.match(answerMessages('q', 'oficial', 'achei isso')[1].content, /oficial\n\nPESQUISA NA WEB \(pode estar desatualizada\):\nachei isso/);
  assert.match(answerMessages('q', '', 'achei')[1].content, /^CONTEXTO:\nPESQUISA NA WEB/);
});

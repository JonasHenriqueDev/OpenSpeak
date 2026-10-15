import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  appLabel, applySettings, checkConfig, diff, fill, formatDuration, parseLive, parseStats, planVoiceUpdates, presenceText, serverText,
  statsVars, streamKey, voiceStatusText, VOICE_STATUS_MAX,
} from '../src/live.js';

const cs = { id: 's1', name: 'Ranked', owner: 'Jonas', channel: 'Jogos', app: 'Counter-Strike 2', type: 3, viewers: 2, started: 1000 };
const tela = { id: 's2', name: 'Reunião', owner: 'Ana', channel: 'Trabalho', app: '', type: 2, viewers: 0, started: 2000 };

test('appLabel usa o programa e cai para o tipo da fonte', () => {
  assert.equal(appLabel(cs), 'Counter-Strike 2');
  assert.equal(appLabel(tela), 'Tela inteira');
  assert.equal(appLabel({ type: 1 }), 'Câmera');
  assert.equal(appLabel({}), 'Transmissão');
});

test('fill troca só os marcadores conhecidos', () => {
  assert.equal(fill('{quem} em {app} {nada}', { quem: 'A', app: 'B' }), 'A em B {nada}');
});

test('parseLive valida, normaliza e ordena por início', () => {
  const list = parseLive({ streams: [{ ...tela }, { ...cs }, { name: 'sem id' }] });
  assert.deepEqual(list.map(s => s.id), ['s1', 's2']);
  assert.throws(() => parseLive({}));
});

test('diff detecta começo e fim; id reaproveitado após reinício do servidor é outro stream', () => {
  let d = diff([], [cs]);
  assert.deepEqual(d.started, [cs]);
  d = diff([streamKey(cs)], [cs, tela]);
  assert.deepEqual(d.started, [tela]);
  assert.deepEqual(d.stopped, []);
  d = diff([streamKey(cs), streamKey(tela)], [{ ...cs, started: 9999 }]);
  assert.equal(d.started.length, 1);
  assert.deepEqual(d.stopped.sort(), [streamKey(cs), streamKey(tela)].sort());
});

test('status do canal de voz lista quem e qual app, respeitando o limite', () => {
  const cfg = { live: '🔴 Ao vivo: {lista}', item: '{quem} ({app})', idle: '' };
  assert.equal(voiceStatusText(cfg, [cs, tela]), '🔴 Ao vivo: Jonas (Counter-Strike 2), Ana (Tela inteira)');
  assert.equal(voiceStatusText(cfg, []), '');
  const many = Array.from({ length: 80 }, (_, i) => ({ ...cs, id: `s${i}`, owner: `Pessoa${i}` }));
  assert.equal(voiceStatusText(cfg, many).length, VOICE_STATUS_MAX);
});

test('status do bot: um, vários e ninguém', () => {
  const cfg = { live: '{quem} transmitindo {app}', liveMany: '{qtd} ao vivo', idle: 'Ninguém ao vivo' };
  assert.equal(presenceText(cfg, [cs]), 'Jonas transmitindo Counter-Strike 2');
  assert.equal(presenceText(cfg, [cs, tela]), '2 ao vivo');
  assert.equal(presenceText({ ...cfg, liveMany: '' }, [cs, tela]), 'Jonas transmitindo Counter-Strike 2');
  assert.equal(presenceText(cfg, []), 'Ninguém ao vivo');
});

test('carga do servidor: stats do /api/live viram marcadores', () => {
  const stats = parseStats({ streams: [], stats: { clients: 7, streams: 3, viewers: 5, mbps: 12.34 } });
  assert.deepEqual(stats, { clients: 7, streams: 3, viewers: 5, mbps: 12.34 });
  const v = statsVars(stats);
  assert.equal(v.usuarios, '7');
  assert.equal(v.mbps, '12');
  assert.equal(v.trafego, ' · 12 Mbps');
  assert.equal(statsVars({ mbps: 2.46 }).mbps, '2,5');
  // Servidor antigo (sem stats) ou P2P (mbps null): sem número inventado.
  assert.deepEqual(parseStats({ streams: [] }), { clients: null, streams: null, viewers: null, mbps: null });
  assert.equal(statsVars(parseStats({ stats: { clients: 2, mbps: null } })).trafego, '');
  assert.equal(statsVars(parseStats(null)).usuarios, '—');
});

test('status do bot: texto da carga do servidor e marcadores nos outros textos', () => {
  const cfg = { idle: 'Ninguém · {usuarios} online', live: '{quem} · {assistindo} assistindo', server: '👥 {usuarios} online{trafego}' };
  const stats = { clients: 4, streams: 1, viewers: 2, mbps: 8 };
  assert.equal(presenceText(cfg, [], stats), 'Ninguém · 4 online');
  assert.equal(presenceText(cfg, [cs], stats), 'Jonas · 2 assistindo');
  assert.equal(serverText(cfg, stats), '👥 4 online · 8 Mbps');
  assert.equal(serverText(cfg, parseStats(null)), ''); // servidor não informou: não mostra
  assert.equal(serverText({ ...cfg, server: '' }, stats), '');
  assert.equal(voiceStatusText({ live: '{lista} ({usuarios} online)', item: '{quem}', idle: '' }, [cs], stats), 'Jonas (4 online)');
});

test('formatDuration', () => {
  assert.equal(formatDuration(5 * 60_000), '5 min');
  assert.equal(formatDuration(65 * 60_000), '1 h 05 min');
});

test('checkConfig exige IDs só dos avisos ligados', () => {
  const ok = { openspeakUrl: 'https://openspeak.duckdns.org', voiceStatus: { enabled: false }, chat: { enabled: false }, presence: { enabled: true } };
  assert.deepEqual(checkConfig(ok), []);
  assert.equal(ok.pollSeconds, 15);
  assert.equal(ok.presence.type, 'custom');
  assert.equal(ok.presence.rotateSeconds, 20);
  assert.equal(checkConfig({ ...ok, presence: { type: 'dançando' } }).length, 1);
  const fast = { ...ok, presence: { type: 'playing', rotateSeconds: 1 } };
  assert.deepEqual(checkConfig(fast), []);
  assert.equal(fast.presence.rotateSeconds, 15);
  const bad = { openspeakUrl: 'ftp://x', chat: { enabled: true, channelId: 'abc', onEnd: 'sumir' } };
  assert.equal(checkConfig(bad).length, 3);
});

test('status de voz automático: escreve nas calls com gente e limpa as que esvaziaram', () => {
  const current = new Map([['a', 'velho'], ['b', 'ao vivo']]);
  assert.deepEqual(planVoiceUpdates(current, ['b', 'c'], 'ao vivo'), [
    { channelId: 'c', status: 'ao vivo' },
    { channelId: 'a', status: '' },
  ]);
  // ninguém ao vivo: limpa tudo que o bot escreveu, mesmo em calls com gente
  assert.deepEqual(planVoiceUpdates(new Map([['b', 'ao vivo']]), ['b'], ''), [{ channelId: 'b', status: '' }]);
  assert.deepEqual(planVoiceUpdates(new Map(), ['b'], ''), []);
});

test('checkConfig aceita status de voz sem canal (automático)', () => {
  const cfg = { openspeakUrl: 'https://x.y', voiceStatus: { enabled: true, channelId: '' } };
  assert.deepEqual(checkConfig(cfg), []);
  assert.equal(checkConfig({ ...cfg, guildId: 'abc' }).length, 1);
});

test('applySettings: escolhas do /openspeak por cima do config.json, sem alterá-lo', () => {
  const base = { openspeakUrl: 'https://x.y', chat: { enabled: false, channelId: '', onEnd: 'edit' },
    voiceStatus: { enabled: true, channelId: '' }, presence: { enabled: true } };
  const out = applySettings(base, { chat: { enabled: true, channelId: '123456789012345678' }, presence: { enabled: false } });
  assert.equal(out.chat.channelId, '123456789012345678');
  assert.equal(out.chat.enabled, true);
  assert.equal(out.chat.onEnd, 'edit');
  assert.equal(out.presence.enabled, false);
  assert.equal(out.voiceStatus.enabled, true);
  assert.equal(base.chat.enabled, false);
  assert.deepEqual(applySettings(base, {}), base);
});

test('checkConfig: chat ligado sem canal espera o /openspeak canal', () => {
  assert.deepEqual(checkConfig({ openspeakUrl: 'https://x.y', chat: { enabled: true, channelId: '' } }), []);
});

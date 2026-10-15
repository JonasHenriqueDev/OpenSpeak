import { test } from 'node:test';
import assert from 'node:assert/strict';
import { startWebhook } from '../src/webhook.js';
import { checkConfig } from '../src/live.js';

async function withWebhook(secret, fn) {
  const received = [];
  const server = await startWebhook({ port: 0, secret, onLive: streams => received.push(streams) });
  const url = `http://127.0.0.1:${server.address().port}`;
  try {
    await fn(url, received);
  } finally {
    await new Promise(resolve => server.close(resolve));
  }
}

const body = JSON.stringify({ server: 'OpenSpeak', streams: [{ id: 's1', owner: 'Jonas', app: 'Chrome', started: 1 }] });

test('webhook entrega a lista ao bot quando o segredo confere', () => withWebhook('abc', async (url, received) => {
  const res = await fetch(`${url}/live`, { method: 'POST', headers: { 'x-openspeak-secret': 'abc' }, body });
  assert.equal(res.status, 204);
  assert.equal(received.length, 1);
  assert.equal(received[0][0].owner, 'Jonas');
}));

test('webhook recusa segredo errado, rota errada e corpo inválido', () => withWebhook('abc', async (url, received) => {
  assert.equal((await fetch(`${url}/live`, { method: 'POST', headers: { 'x-openspeak-secret': 'x' }, body })).status, 401);
  assert.equal((await fetch(`${url}/live`, { method: 'POST', body })).status, 401);
  assert.equal((await fetch(`${url}/outra`, { method: 'POST', body })).status, 404);
  assert.equal((await fetch(`${url}/live`)).status, 404);
  const bad = await fetch(`${url}/live`, { method: 'POST', headers: { 'x-openspeak-secret': 'abc' }, body: '{"nada":1}' });
  assert.equal(bad.status, 400);
  assert.equal(received.length, 0);
}));

test('webhook sem segredo aceita qualquer POST em /live', () => withWebhook('', async (url, received) => {
  assert.equal((await fetch(`${url}/live`, { method: 'POST', body })).status, 204);
  assert.equal(received.length, 1);
}));

test('checkConfig: webhook desligado por padrão e porta validada', () => {
  const cfg = { openspeakUrl: 'https://x.y' };
  assert.deepEqual(checkConfig(cfg), []);
  assert.equal(cfg.webhook.port, 0);
  assert.equal(cfg.webhook.host, '127.0.0.1');
  assert.equal(checkConfig({ openspeakUrl: 'https://x.y', webhook: { port: 70000 } }).length, 1);
});

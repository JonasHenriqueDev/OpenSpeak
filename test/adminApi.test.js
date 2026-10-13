import test from 'node:test';
import assert from 'node:assert/strict';
import { handleAdmin, issueSession, verifySession, SESSION_TTL_S } from '../server/adminApi.js';

const info = { version: 'test', env: 'test', redis: true };
function setup(overrides = {}) {
  const calls = [], hits = new Map();
  const room = {
    status: async () => ({ clients: [], bans: [], locked: false, streams: 0, maxClients: 32 }),
    kick: async (clid, opts) => { calls.push(['kick', clid, opts]); return { clid }; },
    setLocked: async locked => { calls.push(['lock', locked]); return { locked }; },
    reset: async () => ({ disconnected: 0 }), unban: async () => ({}),
  };
  const store = { ping: async () => true, hit: async key => { hits.set(key, (hits.get(key) || 0) + 1); return hits.get(key); } };
  const ctx = { token: 'secret-token', room: () => room, store: () => store, info, ...overrides };
  const call = (action, { method = 'GET', body, cookie = '', csrf = true } = {}) => handleAdmin({ method, action, body, ip: '1.2.3.4',
    headers: { cookie, ...(method === 'POST' && csrf ? { 'x-openspeak-admin': '1' } : {}) } }, ctx);
  return { call, calls };
}
const cookieOf = res => res.headers['set-cookie'].split(';')[0];

test('session cookies are signed, expire and are tied to the token', () => {
  const value = issueSession('a', 0);
  assert.ok(verifySession('a', value, 1000));
  assert.ok(!verifySession('b', value, 1000));
  assert.ok(!verifySession('a', value, (SESSION_TTL_S + 1) * 1000));
  assert.ok(!verifySession('a', value.replace(/.$/, c => (c === 'A' ? 'B' : 'A')), 1000));
});

test('panel is disabled without ADMIN_TOKEN and requires a session', async () => {
  const off = setup({ token: '' });
  assert.equal((await off.call('status')).status, 503);
  const { call } = setup();
  assert.equal((await call('status')).status, 401);
  assert.equal((await call('status', { cookie: 'os_admin=123.forged' })).status, 401);
});

test('login rejects wrong passwords, rate-limits and grants a working cookie', async () => {
  const { call, calls } = setup();
  assert.equal((await call('login', { method: 'POST', body: { password: 'nope' } })).status, 401);
  const ok = await call('login', { method: 'POST', body: { password: 'secret-token' } });
  assert.equal(ok.status, 200);
  assert.match(ok.headers['set-cookie'], /HttpOnly; Secure; SameSite=Strict/);
  const cookie = cookieOf(ok);
  assert.equal((await call('status', { cookie })).status, 200);
  assert.equal((await call('lock', { method: 'POST', cookie, body: { locked: true } })).status, 200);
  assert.deepEqual(calls.at(-1), ['lock', true]);
  assert.equal((await call('lock', { method: 'POST', cookie, csrf: false, body: { locked: true } })).status, 403);
  for (let i = 0; i < 9; i++) await call('login', { method: 'POST', body: { password: 'x' } });
  assert.equal((await call('login', { method: 'POST', body: { password: 'secret-token' } })).status, 429);
});

test('ban accepts only the offered durations', async () => {
  const { call, calls } = setup();
  const cookie = cookieOf(await call('login', { method: 'POST', body: { password: 'secret-token' } }));
  assert.equal((await call('ban', { method: 'POST', cookie, body: { clid: 3, minutes: 5 } })).status, 400);
  assert.equal((await call('ban', { method: 'POST', cookie, body: { clid: 3, minutes: null } })).status, 200);
  assert.equal((await call('kick', { method: 'POST', cookie, body: { clid: 4 } })).status, 200);
  assert.deepEqual(calls.map(c => [c[1], c[2].minutes]), [[3, null], [4, 0]]);
});

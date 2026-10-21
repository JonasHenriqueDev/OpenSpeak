import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { once } from 'node:events';
import { WebSocketServer } from 'ws';
import { ServerConnection } from '../public/js/core/ServerConnection.js';

async function startServer({ resumeToken }) {
  const server = http.createServer();
  const wss = new WebSocketServer({ server });
  const state = { connections: 0, logins: [], loggedOut: false };
  wss.on('connection', ws => {
    state.connections++;
    ws.on('message', raw => {
      const msg = JSON.parse(raw);
      if (msg.cmd === 'login') {
        state.logins.push(msg.args);
        const data = { clid: 7, server: { name: 'QA' }, channels: [], clients: [], streams: [] };
        if (resumeToken) data.resumeToken = resumeToken;
        if (msg.args.resumeToken) data.resumed = true;
        ws.send(JSON.stringify({ type: 'res', id: msg.id, return_code: 'ok', data }));
      } else if (msg.cmd === 'logout') state.loggedOut = true;
    });
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const close = () => { for (const ws of wss.clients) ws.terminate(); wss.close(); server.close(); };
  return { url: `ws://127.0.0.1:${server.address().port}/ws`, wss, state, close };
}

test('native server session resumes after the socket drops and logout stops it', async () => {
  const srv = await startServer({ resumeToken: 'qa-token' });
  const client = new ServerConnection();
  const events = [];
  for (const name of ['reconnecting', 'resumed', 'disconnected']) client.on(name, () => events.push(name));
  try {
    await client.connect(srv.url);
    const login = await client.sendCommand('login', { nickname: 'QA', uid: 'qa' });
    assert.equal(login.clid, 7);
    assert.equal(srv.state.logins[0].resumable, true);
    const resumed = new Promise(resolve => client.on('resumed', resolve));
    [...srv.wss.clients][0].terminate(); // queda sem close handshake
    const view = await resumed;
    assert.equal(view.clid, 7);
    assert.deepEqual({ clid: srv.state.logins[1].resumeClid, token: srv.state.logins[1].resumeToken }, { clid: 7, token: 'qa-token' });
    assert.deepEqual(events, ['reconnecting', 'resumed']);
    assert.equal(srv.state.connections, 2);

    const gone = new Promise(resolve => client.on('disconnected', resolve));
    client.disconnect();
    await gone;
    await new Promise(resolve => setTimeout(resolve, 1200));
    assert.ok(srv.state.loggedOut);
    assert.equal(srv.state.connections, 2);
    assert.deepEqual(events, ['reconnecting', 'resumed', 'disconnected']);
  } finally {
    client.disconnect();
    srv.close();
  }
});

test('server without resumeToken keeps the old behaviour: a drop disconnects', async () => {
  const srv = await startServer({ resumeToken: null });
  const client = new ServerConnection();
  try {
    await client.connect(srv.url);
    await client.sendCommand('login', { nickname: 'QA', uid: 'qa' });
    const gone = new Promise(resolve => client.on('disconnected', resolve));
    [...srv.wss.clients][0].terminate();
    await gone;
    await new Promise(resolve => setTimeout(resolve, 1200));
    assert.equal(srv.state.connections, 1);
    await assert.rejects(client.sendCommand('sync'), { returnCode: 'disconnected' });
  } finally {
    client.disconnect();
    srv.close();
  }
});

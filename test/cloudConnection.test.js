import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { once } from 'node:events';
import { WebSocketServer } from 'ws';
import { CloudConnection } from '../public/js/core/CloudConnection.js';

test('cloud client resumes after socket rotation and stops reconnecting after logout', async () => {
  const server = http.createServer();
  const wss = new WebSocketServer({ server });
  let connections = 0, resumed = false, loggedOut = false;
  wss.on('connection', ws => {
    connections++;
    ws.on('message', raw => {
      const msg = JSON.parse(raw);
      if (msg.cmd === 'login') {
        resumed ||= msg.args.resumeClid === 7 && msg.args.resumeToken === 'test-token';
        ws.send(JSON.stringify({ type: 'res', id: msg.id, return_code: 'ok', data: { clid: 7, resumeToken: 'test-token' } }));
      } else if (msg.cmd === 'logout') loggedOut = true;
    });
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const client = new CloudConnection();
  try {
    await client.connect(`ws://127.0.0.1:${server.address().port}`);
    await client.sendCommand('login', { nickname: 'QA', uid: 'qa', accessCode: 'test-code' });
    const recovery = new Promise(resolve => client.on('resumed', resolve));
    [...wss.clients][0].close();
    const view = await recovery;
    assert.equal(view.clid, 7);
    assert.ok(resumed);
    assert.equal(connections, 2);
    client.disconnect();
    await new Promise(resolve => setTimeout(resolve, 1200));
    assert.ok(loggedOut);
    assert.equal(connections, 2);
    assert.equal(client.auth, null);
  } finally {
    client.disconnect();
    for (const ws of wss.clients) ws.terminate();
    await new Promise(resolve => wss.close(resolve));
    await new Promise(resolve => server.close(resolve));
  }
});

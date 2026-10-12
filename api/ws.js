import http from 'node:http';
import { WebSocketServer } from 'ws';
import { CloudRoom } from '../server/cloudRoom.js';
import { CommandError } from '../server/virtualServer.js';
import { createRedisRoomStore } from '../server/redisRoomStore.js';

const server = http.createServer((req, res) => {
  res.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  res.end(JSON.stringify({ service: 'OpenSpeak signaling', configured: !!process.env.REDIS_URL, roomAccess: 'public' }));
});
const wss = new WebSocketServer({ server, maxPayload: 160 * 1024 });
const sockets = new Map();
let room;
function getRoom() {
  if (!room) room = new CloudRoom(createRedisRoomStore(process.env.REDIS_URL, events => {
    for (const { clid, message } of events) {
      const ws = sockets.get(clid);
      if (ws?.readyState === ws.OPEN) ws.send(JSON.stringify(message));
    }
  }));
  return room;
}
wss.on('connection', ws => {
  if (wss.clients.size > 64) { ws.close(1013, 'Servidor ocupado'); return; }
  let session = {}, queue = Promise.resolve(), count = 0, windowAt = Date.now();
  const authDeadline = setTimeout(() => { if (!session.clid) ws.close(1008, 'Autenticação necessária'); }, 15_000);
  authDeadline.unref();
  ws.on('message', raw => {
    if (Date.now() - windowAt > 60_000) { count = 0; windowAt = Date.now(); }
    if (++count > 180) { ws.close(1008, 'Muitas mensagens'); return; }
    let msg;
    try { msg = JSON.parse(raw.toString()); } catch { ws.close(1008, 'Mensagem inválida'); return; }
    if (msg?.type !== 'cmd' || typeof msg.cmd !== 'string') return;
    queue = queue.then(async () => {
      const reply = { type: 'res', id: msg.id };
      try {
        if (msg.cmd !== 'login' && !session.clid) throw new CommandError('not_logged_in', 'Autenticação necessária.');
        if (msg.cmd === 'login' && session.clid) throw new CommandError('invalid_args', 'Já conectado');
        if (!process.env.REDIS_URL)
          throw new CommandError('not_configured', 'Sala ainda não configurada pelo administrador.');
        const result = await getRoom().execute(session, msg.cmd, msg.args);
        session = result.session;
        sockets.set(session.clid, ws);
        clearTimeout(authDeadline);
        reply.data = result.data;
        reply.return_code = 'ok';
        if (msg.cmd === 'logout' && sockets.get(session.clid) === ws) sockets.delete(session.clid);
      } catch (err) {
        reply.return_code = err instanceof CommandError ? err.code : 'service_unavailable';
        reply.error = err instanceof CommandError ? err.message : 'Sinalização indisponível. Tente novamente.';
      }
      if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(reply));
    }).catch(() => ws.close(1011, 'Sinalização indisponível'));
  });
  ws.on('close', () => {
    clearTimeout(authDeadline);
    if (sockets.get(session.clid) === ws) sockets.delete(session.clid);
    // Preserve room/media state for 90 seconds so function rotation can resume.
  });
});

export default server;

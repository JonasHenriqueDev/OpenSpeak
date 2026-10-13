// Ponto de entrada: serve o cliente web (public/) e a sinalização via WebSocket (/ws).
//
// Variáveis de ambiente:
//   PORT                 porta (padrão 9988)
//   HOST                 interface (padrão 0.0.0.0)
//   OPENSPEAK_NAME       nome do servidor
//   OPENSPEAK_CERT/KEY   caminhos de certificado PEM para HTTPS (necessário para
//                        captura de tela fora de localhost)
//   OPENSPEAK_ICE        JSON com a lista de iceServers (padrão: STUN público)

import http from 'node:http';
import https from 'node:https';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { VirtualServer, CommandError } from './virtualServer.js';
import { ReturnCode } from './protocol.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'public');
const PORT = Number(process.env.PORT ?? 9988);
const HOST = process.env.HOST ?? '0.0.0.0';
const MAX_MESSAGE_BYTES = 512 * 1024;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.json': 'application/json; charset=utf-8',
  '.mp3': 'audio/mpeg',
  '.wav': 'audio/wav',
};

function parseIceServers() {
  if (!process.env.OPENSPEAK_ICE) return [{ urls: 'stun:stun.l.google.com:19302' }];
  try {
    const parsed = JSON.parse(process.env.OPENSPEAK_ICE);
    if (Array.isArray(parsed)) return parsed;
  } catch (err) {
    console.warn('[openspeak] OPENSPEAK_ICE inválido, usando padrão:', err.message);
  }
  return [{ urls: 'stun:stun.l.google.com:19302' }];
}

function serveStatic(req, res) {
  let pathname;
  try {
    pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  } catch {
    res.writeHead(400).end();
    return;
  }
  if (pathname === '/') pathname = '/index.html';
  const file = path.resolve(ROOT, '.' + pathname);
  if (!file.startsWith(ROOT + path.sep)) {
    res.writeHead(403).end();
    return;
  }
  fs.stat(file, (err, stat) => {
    if (err || !stat.isFile()) {
      res.writeHead(404, { 'content-type': 'text/plain' }).end('not found');
      return;
    }
    res.writeHead(200, {
      'content-type': MIME[path.extname(file)] ?? 'application/octet-stream',
      'cache-control': 'no-cache',
    });
    fs.createReadStream(file).pipe(res);
  });
}

const cert = process.env.OPENSPEAK_CERT;
const key = process.env.OPENSPEAK_KEY;
const secure = !!(cert && key);
const httpServer = secure
  ? https.createServer({ cert: fs.readFileSync(cert), key: fs.readFileSync(key) }, serveStatic)
  : http.createServer(serveStatic);

const sockets = new Map(); // clid -> ws

const server = new VirtualServer({
  name: process.env.OPENSPEAK_NAME ?? 'OpenSpeak',
  iceServers: parseIceServers(),
  send(clid, message) {
    const ws = sockets.get(clid);
    if (ws && ws.readyState === ws.OPEN) ws.send(JSON.stringify(message));
  },
});

const wss = new WebSocketServer({ server: httpServer, path: '/ws', maxPayload: MAX_MESSAGE_BYTES });

wss.on('connection', (ws, req) => {
  const clid = server.connect();
  sockets.set(clid, ws);
  ws.isAlive = true;
  console.log(`[openspeak] conexão clid=${clid} de ${req.socket.remoteAddress}`);

  const ping = () => {
    ws.pingPayload = Buffer.from(String(Date.now()));
    ws.pingStarted = process.hrtime.bigint();
    ws.ping(ws.pingPayload);
  };
  ws.measurePing = ping;
  ws.on('pong', payload => {
    ws.isAlive = true;
    if (ws.pingStarted && payload.equals(ws.pingPayload)) {
      server.setClientLatency(clid, Number(process.hrtime.bigint() - ws.pingStarted) / 1e6);
      ws.pingStarted = null;
    }
  });
  ping();

  ws.on('message', raw => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      return;
    }
    if (msg?.type !== 'cmd' || typeof msg.cmd !== 'string') return;
    const reply = { type: 'res', id: msg.id };
    try {
      reply.data = server.handle(clid, msg.cmd, msg.args) ?? {};
      reply.return_code = ReturnCode.OK;
    } catch (err) {
      if (err instanceof CommandError) {
        reply.return_code = err.code;
        reply.error = err.message;
      } else {
        console.error(`[openspeak] erro em ${msg.cmd}:`, err);
        reply.return_code = 'internal_error';
        reply.error = 'erro interno';
      }
    }
    if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(reply));
  });

  ws.on('close', () => {
    sockets.delete(clid);
    server.disconnect(clid);
    console.log(`[openspeak] desconectado clid=${clid}`);
  });
});

// Keep-alive: derruba sockets mortos para liberar streams presos.
const heartbeat = setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) {
      ws.terminate();
      continue;
    }
    ws.isAlive = false;
    ws.measurePing();
  }
}, 15_000);
wss.on('close', () => clearInterval(heartbeat));

httpServer.listen(PORT, HOST, () => {
  const scheme = secure ? 'https' : 'http';
  console.log(`[openspeak] servidor "${server.name}" em ${scheme}://localhost:${PORT}`);
  if (!secure) console.log('[openspeak] dica: fora de localhost o navegador exige HTTPS para capturar tela/câmera (ver README).');
});

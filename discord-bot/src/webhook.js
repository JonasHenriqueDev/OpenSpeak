// Receptor do webhook do servidor OpenSpeak (OPENSPEAK_LIVE_WEBHOOK): o servidor
// manda um POST com a mesma lista do GET /api/live assim que algo muda, e o bot
// atualiza o Discord na hora. Escuta só em 127.0.0.1 por padrão (mesma máquina).

import http from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';
import { parseLive, parseStats } from './live.js';

const MAX_BODY = 256 * 1024;
const sha = value => createHash('sha256').update(String(value)).digest();

/**
 * @param {object} options
 * @param {number} options.port
 * @param {string} [options.host]   padrão 127.0.0.1
 * @param {string} [options.secret] exigido no header X-OpenSpeak-Secret (vazio = sem segredo)
 * @param {(streams: object[], stats: object) => void} options.onLive
 * @returns {Promise<http.Server>}
 */
export function startWebhook({ port, host = '127.0.0.1', secret = '', onLive }) {
  const server = http.createServer((req, res) => {
    const reply = (status, text = '') => res.writeHead(status, { 'content-type': 'text/plain; charset=utf-8' }).end(text);
    if (req.method !== 'POST' || req.url.split('?')[0] !== '/live') return reply(404);
    if (secret && !timingSafeEqual(sha(req.headers['x-openspeak-secret'] ?? ''), sha(secret))) return reply(401);
    const chunks = [];
    let size = 0;
    req.on('data', chunk => {
      size += chunk.length;
      if (size > MAX_BODY) {
        reply(413);
        req.destroy();
      } else {
        chunks.push(chunk);
      }
    });
    req.on('end', () => {
      if (res.writableEnded) return;
      let streams, stats;
      try {
        const body = JSON.parse(Buffer.concat(chunks).toString('utf8'));
        streams = parseLive(body);
        stats = parseStats(body);
      } catch {
        return reply(400);
      }
      reply(204);
      onLive(streams, stats);
    });
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => {
      server.off('error', reject);
      resolve(server);
    });
  });
}

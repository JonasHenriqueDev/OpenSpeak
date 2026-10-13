// Admin panel API: password login -> signed HttpOnly cookie -> room moderation.
// Kept free of Vercel specifics so it can be tested with a fake store.

import { createHash, createHmac, timingSafeEqual } from 'node:crypto';
import { CommandError } from './virtualServer.js';
import { hashIp } from './cloudRoom.js';

export const SESSION_TTL_S = 12 * 60 * 60;
const COOKIE = 'os_admin';
const LOGIN_LIMIT = 10, LOGIN_WINDOW_S = 15 * 60;
const BAN_MINUTES = new Set([10, 60, 24 * 60, 7 * 24 * 60]);

const sha = value => createHash('sha256').update(value).digest();
const safeEqual = (a, b) => timingSafeEqual(sha(String(a)), sha(String(b)));
// Rotating ADMIN_TOKEN invalidates every issued session.
const sign = (token, payload) => createHmac('sha256', `openspeak-admin:${token}`).update(payload).digest('base64url');

export function issueSession(token, now = Date.now()) {
  const exp = String(Math.floor(now / 1000) + SESSION_TTL_S);
  return `${exp}.${sign(token, exp)}`;
}

export function verifySession(token, value, now = Date.now()) {
  const [exp, mac] = String(value || '').split('.');
  if (!exp || !mac || !/^\d+$/.test(exp)) return false;
  return safeEqual(mac, sign(token, exp)) && Number(exp) * 1000 > now;
}

const readCookie = (header = '', name) =>
  header.split(';').map(p => p.trim().split('=')).find(([k]) => k === name)?.slice(1).join('=') ?? '';

function cookie(value, maxAge) {
  return `${COOKIE}=${value}; Path=/api/admin; HttpOnly; Secure; SameSite=Strict; Max-Age=${maxAge}`;
}

/**
 * @param {object} ctx
 * @param {string} ctx.token  ADMIN_TOKEN (empty = panel disabled)
 * @param {() => import('./cloudRoom.js').CloudRoom} ctx.room
 * @param {() => object} ctx.store  redis store (ping/hit)
 * @param {object} ctx.info   static deployment info shown in the panel
 */
export async function handleAdmin({ method, action, headers, body, ip }, { token, room, store, info, now = Date.now }) {
  const reply = (status, data, extraHeaders = {}) => ({ status, data, headers: extraHeaders });
  if (!token) return reply(503, { error: 'Painel desativado: defina ADMIN_TOKEN nas variáveis de ambiente da Vercel.' });
  const mutating = method === 'POST';
  // A custom header cannot be sent cross-site without a CORS preflight, which we never grant.
  if (mutating && headers['x-openspeak-admin'] !== '1') return reply(403, { error: 'Requisição recusada.' });

  if (action === 'login') {
    if (!mutating) return reply(405, { error: 'Use POST.' });
    if (!info.redis) return reply(503, { error: 'Redis não configurado (REDIS_URL).' });
    const attempts = await store().hit(`admin-login:${hashIp(ip) || 'unknown'}`, LOGIN_WINDOW_S);
    if (attempts > LOGIN_LIMIT) return reply(429, { error: 'Muitas tentativas. Aguarde 15 minutos.' });
    if (typeof body?.password !== 'string' || !safeEqual(body.password, token))
      return reply(401, { error: 'Senha incorreta.' });
    return reply(200, { ok: true }, { 'set-cookie': cookie(issueSession(token, now()), SESSION_TTL_S) });
  }
  if (action === 'logout') return reply(200, { ok: true }, { 'set-cookie': cookie('', 0) });

  if (!verifySession(token, readCookie(headers.cookie, COOKIE), now())) return reply(401, { error: 'Sessão expirada.' });
  if (!info.redis) return reply(200, { info, redis: false });

  try {
    if (action === 'status' && !mutating) {
      const redis = await store().ping().catch(() => false);
      return reply(200, { info, redis, ...(redis ? await room().status() : {}) });
    }
    if (!mutating) return reply(405, { error: 'Use POST.' });
    switch (action) {
      case 'kick':
      case 'ban': {
        let minutes = 0;
        if (action === 'ban') {
          minutes = body?.minutes === null ? null : Number(body?.minutes);
          if (minutes !== null && !BAN_MINUTES.has(minutes)) return reply(400, { error: 'Duração inválida.' });
        }
        return reply(200, await room().kick(Number(body?.clid), { minutes, reason: body?.reason ?? '' }));
      }
      case 'unban': return reply(200, await room().unban(String(body?.id ?? '')));
      case 'lock': return reply(200, await room().setLocked(!!body?.locked));
      case 'reset': return reply(200, await room().reset());
      default: return reply(404, { error: 'Ação desconhecida.' });
    }
  } catch (err) {
    if (err instanceof CommandError) return reply(err.code === 'busy' ? 503 : 400, { error: err.message });
    console.warn('Admin: falha ao acessar o armazenamento');
    return reply(503, { error: 'Armazenamento indisponível. Tente novamente.' });
  }
}

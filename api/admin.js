import { CloudRoom } from '../server/cloudRoom.js';
import { createRedisRoomStore } from '../server/redisRoomStore.js';
import { handleAdmin } from '../server/adminApi.js';
import pkg from '../package.json' with { type: 'json' };

let store, room;
const getStore = () => (store ??= createRedisRoomStore(process.env.REDIS_URL));
const getRoom = () => (room ??= new CloudRoom(getStore()));

async function readJson(req) {
  if (req.method !== 'POST') return {};
  let raw = '';
  for await (const chunk of req) {
    raw += chunk;
    if (raw.length > 4096) return {};
  }
  try { return JSON.parse(raw || '{}'); } catch { return {}; }
}

export default async function handler(req, res) {
  const url = new URL(req.url, 'http://localhost');
  const result = await handleAdmin({
    method: req.method,
    action: url.searchParams.get('action') || '',
    headers: req.headers,
    body: await readJson(req),
    ip: String(req.headers['x-real-ip'] || req.headers['x-forwarded-for'] || '').split(',')[0].trim(),
  }, {
    token: process.env.ADMIN_TOKEN || '',
    room: getRoom,
    store: getStore,
    info: {
      version: pkg.version,
      env: process.env.VERCEL_ENV || 'local',
      commit: (process.env.VERCEL_GIT_COMMIT_SHA || '').slice(0, 7),
      region: process.env.VERCEL_REGION || '',
      redis: !!process.env.REDIS_URL,
    },
  });
  res.writeHead(result.status, { 'content-type': 'application/json', 'cache-control': 'no-store', ...result.headers });
  res.end(JSON.stringify(result.data));
}

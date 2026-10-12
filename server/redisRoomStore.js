import Redis from 'ioredis';

const COMMIT = `
local current = redis.call('GET', KEYS[1]) or ''
if current ~= ARGV[1] then return 0 end
redis.call('SET', KEYS[1], ARGV[2], 'EX', 300)
redis.call('PUBLISH', KEYS[2], ARGV[3])
return 1`;

export function createRedisRoomStore(url, onEvents) {
  if (!url) throw new Error('Redis não configurado');
  const options = { maxRetriesPerRequest: 1, connectTimeout: 5000, retryStrategy: n => Math.min(n * 500, 5000) };
  const redis = new Redis(url, options);
  const subscriber = new Redis(url, options);
  // Avoid logging connection URLs or passwords from driver errors.
  redis.on('error', () => console.warn('Armazenamento de sinalização indisponível'));
  subscriber.on('error', () => console.warn('Notificações de sinalização indisponíveis'));
  const key = 'openspeak:v1:room', channel = 'openspeak:v1:events';
  subscriber.on('message', (_, payload) => {
    try { onEvents(JSON.parse(payload)); } catch { /* malformed events ignored */ }
  });
  const ready = subscriber.subscribe(channel);
  return {
    async read() { await ready; return redis.get(key); },
    async commit(before, after, events) {
      return Number(await redis.eval(COMMIT, 2, key, channel, before || '', after, JSON.stringify(events))) === 1;
    },
    async close() { await Promise.allSettled([redis.quit(), subscriber.quit()]); },
  };
}

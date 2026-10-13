import Redis from 'ioredis';

const COMMIT = `
local current = redis.call('GET', KEYS[1]) or ''
if current ~= ARGV[1] then return 0 end
redis.call('SET', KEYS[1], ARGV[2], 'EX', 300)
redis.call('PUBLISH', KEYS[2], ARGV[3])
return 1`;
// Policy (lock + bans) must outlive the idle room, so it has no expiry.
const COMMIT_POLICY = `
local current = redis.call('GET', KEYS[1]) or ''
if current ~= ARGV[1] then return 0 end
redis.call('SET', KEYS[1], ARGV[2])
return 1`;

/** Without `onEvents` (admin API) no subscriber connection is opened. */
export function createRedisRoomStore(url, onEvents) {
  if (!url) throw new Error('Redis não configurado');
  const options = { maxRetriesPerRequest: 1, connectTimeout: 5000, retryStrategy: n => Math.min(n * 500, 5000) };
  const redis = new Redis(url, options);
  const subscriber = onEvents ? new Redis(url, options) : null;
  // Avoid logging connection URLs or passwords from driver errors.
  redis.on('error', () => console.warn('Armazenamento de sinalização indisponível'));
  subscriber?.on('error', () => console.warn('Notificações de sinalização indisponíveis'));
  const key = 'openspeak:v1:room', policyKey = 'openspeak:v1:policy', channel = 'openspeak:v1:events';
  subscriber?.on('message', (_, payload) => {
    try { onEvents(JSON.parse(payload)); } catch { /* malformed events ignored */ }
  });
  const ready = subscriber ? subscriber.subscribe(channel) : Promise.resolve();
  return {
    async read() { await ready; return redis.get(key); },
    async commit(before, after, events) {
      return Number(await redis.eval(COMMIT, 2, key, channel, before || '', after, JSON.stringify(events))) === 1;
    },
    readPolicy() { return redis.get(policyKey); },
    async commitPolicy(before, after) {
      return Number(await redis.eval(COMMIT_POLICY, 1, policyKey, before || '', after)) === 1;
    },
    async ping() { return (await redis.ping()) === 'PONG'; },
    /** Fixed-window counter used to rate-limit admin logins. */
    async hit(name, ttlSeconds) {
      const k = `openspeak:v1:limit:${name}`;
      const [[, count]] = await redis.multi().incr(k).expire(k, ttlSeconds, 'NX').exec();
      return Number(count);
    },
    async close() { await Promise.allSettled([redis.quit(), subscriber?.quit()]); },
  };
}

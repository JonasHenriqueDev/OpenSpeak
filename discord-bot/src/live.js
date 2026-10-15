// Lógica pura do bot (sem Discord): lê o GET /api/live do servidor OpenSpeak,
// descobre o que começou/terminou e monta os textos a partir dos modelos da config.

/** Rótulo quando o cliente não informa o programa (navegador, tela, câmera). */
const TYPE_LABEL = { 1: 'Câmera', 2: 'Tela inteira', 3: 'Janela', 4: 'Transmissão' };

export const VOICE_STATUS_MAX = 500; // limite do Discord
export const PRESENCE_MAX = 128;
export const ON_END = ['edit', 'delete', 'keep'];

export function appLabel(stream) {
  return (typeof stream.app === 'string' && stream.app.trim()) || TYPE_LABEL[stream.type] || 'Transmissão';
}

/** Ids de stream recomeçam ("s1") quando o servidor reinicia; o início desempata. */
export const streamKey = stream => `${stream.id}@${stream.started}`;

export function fill(template, vars) {
  return String(template ?? '').replace(/\{(\w+)\}/g, (match, key) => (key in vars ? String(vars[key]) : match));
}

export function streamVars(stream, total = 1) {
  return {
    quem: stream.owner || 'Alguém',
    app: appLabel(stream),
    titulo: stream.name || '',
    canal: stream.channel || '',
    espectadores: stream.viewers ?? 0,
    qtd: total,
  };
}

const clip = (text, max) => (text.length > max ? `${text.slice(0, max - 1)}…` : text);

/** Valida a resposta do servidor; lança se não for o formato esperado. */
export function parseLive(body) {
  if (!body || !Array.isArray(body.streams)) throw new Error('resposta inesperada de /api/live');
  return body.streams
    .filter(s => s && typeof s.id === 'string')
    .map(s => ({
      id: s.id,
      name: String(s.name ?? ''),
      owner: String(s.owner ?? ''),
      channel: String(s.channel ?? ''),
      app: String(s.app ?? ''),
      type: Number(s.type) || 0,
      paused: !!s.paused,
      viewers: Number(s.viewers) || 0,
      started: Number(s.started) || 0,
    }))
    .sort((a, b) => a.started - b.started);
}

/** @param {Set<string>|string[]} previous chaves já anunciadas */
export function diff(previous, streams) {
  const before = new Set(previous);
  const now = new Set(streams.map(streamKey));
  return {
    started: streams.filter(s => !before.has(streamKey(s))),
    stopped: [...before].filter(key => !now.has(key)),
  };
}

export function voiceStatusText(cfg, streams) {
  if (!streams.length) return clip(fill(cfg.idle, { qtd: 0, lista: '' }), VOICE_STATUS_MAX);
  const lista = streams.map(s => fill(cfg.item, streamVars(s, streams.length))).join(', ');
  const first = streamVars(streams[0], streams.length);
  return clip(fill(cfg.live, { ...first, lista }), VOICE_STATUS_MAX);
}

export function presenceText(cfg, streams) {
  if (!streams.length) return clip(fill(cfg.idle, { qtd: 0 }), PRESENCE_MAX);
  const template = streams.length > 1 && cfg.liveMany ? cfg.liveMany : cfg.live;
  return clip(fill(template, streamVars(streams[0], streams.length)), PRESENCE_MAX);
}

export function formatDuration(ms) {
  const minutes = Math.max(0, Math.round(ms / 60_000));
  if (minutes < 60) return `${minutes} min`;
  return `${Math.floor(minutes / 60)} h ${String(minutes % 60).padStart(2, '0')} min`;
}

/**
 * O que escrever em cada canal de voz. `current`: status já escrito por canal;
 * `targets`: canais que devem mostrar `text`. Os que saíram da lista são limpos.
 */
export function planVoiceUpdates(current, targets, text) {
  const updates = [];
  const wanted = new Set(text ? targets : []);
  for (const channelId of wanted) if (current.get(channelId) !== text) updates.push({ channelId, status: text });
  for (const [channelId, status] of current) if (!wanted.has(channelId) && status) updates.push({ channelId, status: '' });
  return updates;
}

/** Opção de /openspeak avisos -> seção da config. */
export const NOTICES = { chat: 'chat', voz: 'voiceStatus', status: 'presence' };

/**
 * Config efetiva: o config.json com as escolhas feitas pelo /openspeak por cima
 * (guardadas no state.json). Só o canal do chat e o liga/desliga de cada aviso.
 */
export function applySettings(config, settings = {}) {
  const out = { ...config };
  for (const section of Object.values(NOTICES)) {
    out[section] = { ...config[section] };
    const enabled = settings[section]?.enabled;
    if (typeof enabled === 'boolean') out[section].enabled = enabled;
  }
  if (typeof settings.chat?.channelId === 'string') out.chat.channelId = settings.chat.channelId;
  return out;
}

/** Normaliza a config e devolve a lista de problemas (vazia = ok). */
export function checkConfig(config) {
  const problems = [];
  const id = v => typeof v === 'string' && /^\d{15,25}$/.test(v);
  try {
    const url = new URL(config.openspeakUrl);
    if (!/^https?:$/.test(url.protocol)) problems.push('openspeakUrl deve começar com http:// ou https://');
  } catch {
    problems.push('openspeakUrl inválido');
  }
  config.pollSeconds = Math.min(300, Math.max(5, Number(config.pollSeconds) || 15));
  // Webhook do servidor (OPENSPEAK_LIVE_WEBHOOK): porta 0 = desligado, só a consulta.
  config.webhook = { port: 0, host: '127.0.0.1', secret: '', ...config.webhook };
  config.webhook.port = Number(config.webhook.port) || 0;
  if (process.env.OPENSPEAK_BOT_WEBHOOK_SECRET) config.webhook.secret = process.env.OPENSPEAK_BOT_WEBHOOK_SECRET;
  if (config.webhook.port < 0 || config.webhook.port > 65535) problems.push('webhook.port deve ser de 1 a 65535 (ou 0 para desligar)');
  config.voiceStatus = { enabled: false, live: '🔴 Ao vivo no OpenSpeak: {lista}', item: '{quem} ({app})', idle: '', ...config.voiceStatus };
  config.chat = { enabled: false, mention: '', onEnd: 'edit', ...config.chat };
  config.presence = { enabled: false, live: '{quem} transmitindo {app}', liveMany: '', idle: '', ...config.presence };
  // Sem channelId: o status vai para os canais de voz que têm alguém conectado.
  if (config.voiceStatus.enabled && config.voiceStatus.channelId && !id(config.voiceStatus.channelId))
    problems.push('voiceStatus.channelId precisa ser o ID do canal de voz (ou vazio para os canais com gente)');
  if (config.guildId && !id(config.guildId)) problems.push('guildId precisa ser o ID do servidor do Discord (ou vazio)');
  // Sem channelId o chat espera o /openspeak canal.
  if (config.chat.channelId && !id(config.chat.channelId)) problems.push('chat.channelId precisa ser o ID do canal de texto (ou vazio para escolher com /openspeak canal)');
  if (!ON_END.includes(config.chat.onEnd)) problems.push(`chat.onEnd deve ser ${ON_END.join(', ')}`);
  return problems;
}

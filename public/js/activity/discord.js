// Cliente mínimo do protocolo do Embedded App SDK do Discord (sem a biblioteca).
// O Discord abre a atividade com ?frame_id=…&instance_id=…&platform=… em
// https://<client_id>.discordsays.com e conversa por postMessage com o frame pai:
// HANDSHAKE -> DISPATCH READY -> comandos (FRAME com nonce, resposta com o mesmo nonce).

const Opcode = Object.freeze({ HANDSHAKE: 0, FRAME: 1, CLOSE: 2, HELLO: 3 });
const READY_TIMEOUT_MS = 10_000;
const DISCORD_SUFFIX = ' (discord)';
const MAX_NICKNAME = 32;

export function discordContext(loc = location) {
  const params = new URLSearchParams(loc.search);
  const frameId = params.get('frame_id');
  const clientId = loc.hostname.match(/^(\d+)\.discordsays\.com$/)?.[1] ?? null;
  if (!frameId || !params.get('instance_id')) return null;
  return { frameId, clientId, platform: params.get('platform'), channelId: params.get('channel_id') };
}

/** Apelido no OpenSpeak a partir do usuário do Discord: "Nome (discord)". */
export function discordNickname(user) {
  const name = (user?.global_name || user?.username || 'Discord').trim();
  return name.slice(0, MAX_NICKNAME - DISCORD_SUFFIX.length).trim() + DISCORD_SUFFIX;
}

export class DiscordRpc {
  constructor(ctx) {
    this.ctx = ctx;
    this.source = window.opener ?? window.parent;
    this.origin = '*';
    try { if (document.referrer) this.origin = new URL(document.referrer).origin; } catch {}
    this.pending = new Map();
    this.ready = new Promise((resolve, reject) => {
      this.resolveReady = resolve;
      setTimeout(() => reject(new Error('o Discord não respondeu ao handshake')), READY_TIMEOUT_MS);
    });
    window.addEventListener('message', ev => this.#onMessage(ev));
    this.#post(Opcode.HANDSHAKE, { v: 1, encoding: 'json', client_id: ctx.clientId, frame_id: ctx.frameId });
  }

  #post(opcode, payload) {
    if (this.source && this.source !== window) this.source.postMessage([opcode, payload], this.origin);
  }

  #onMessage(ev) {
    // Só o frame do Discord que abriu a atividade fala com ela.
    if (ev.source !== this.source || !Array.isArray(ev.data)) return;
    const [opcode, frame] = ev.data;
    if (opcode === Opcode.CLOSE) {
      console.warn('[discord] conexão encerrada:', frame);
      for (const { reject } of this.pending.values()) reject(new Error(frame?.message ?? 'encerrado'));
      this.pending.clear();
      return;
    }
    if (opcode !== Opcode.FRAME || !frame) return;
    if (frame.cmd === 'DISPATCH' && frame.evt === 'READY') return this.resolveReady(frame.data);
    const pending = frame.nonce && this.pending.get(frame.nonce);
    if (!pending) return;
    this.pending.delete(frame.nonce);
    if (frame.evt === 'ERROR') pending.reject(Object.assign(new Error(frame.data?.message ?? 'erro do Discord'), { code: frame.data?.code }));
    else pending.resolve(frame.data);
  }

  async command(cmd, args = {}) {
    await this.ready;
    const nonce = crypto.randomUUID();
    return new Promise((resolve, reject) => {
      this.pending.set(nonce, { resolve, reject });
      this.#post(Opcode.FRAME, { cmd, args, nonce });
    });
  }

  /**
   * Usuário do Discord: AUTHORIZE (escopo identify) -> código trocado por token
   * no servidor OpenSpeak (/api/discord/token, onde fica o client secret) ->
   * AUTHENTICATE. Na primeira vez o Discord pede a autorização ao usuário.
   */
  async user() {
    const { code } = await this.command('AUTHORIZE', {
      client_id: this.ctx.clientId, response_type: 'code', state: '', prompt: 'none', scope: ['identify'],
    });
    const response = await fetch(new URL('api/discord/token', location.href), {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ code }),
    });
    const { access_token: accessToken, error } = await response.json().catch(() => ({}));
    if (!response.ok || !accessToken) throw new Error(error ?? `HTTP ${response.status}`);
    const auth = await this.command('AUTHENTICATE', { access_token: accessToken });
    return auth.user;
  }
}

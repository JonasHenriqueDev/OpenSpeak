import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';
import { VirtualServer, CommandError } from './virtualServer.js';

export const SESSION_GRACE_MS = 90_000;
const digest = value => createHash('sha256').update(value).digest('hex');
const secret = () => randomBytes(24).toString('base64url');
const equal = (a, b) => typeof a === 'string' && typeof b === 'string' &&
  timingSafeEqual(Buffer.from(digest(a)), Buffer.from(digest(b)));
const reject = (code, message) => { throw new CommandError(code, message); };

// GET + atomic compare/commit/publish. No instance owns a room; reconnects can
// land anywhere, and concurrent commands retry against the latest snapshot.
export class CloudRoom {
  constructor(store, accessCode, now = Date.now) {
    this.store = store;
    this.accessCode = accessCode;
    this.now = now;
  }

  async execute(session, cmd, args = {}) {
    if (cmd === 'login' && (!this.accessCode || !equal(args.accessCode, this.accessCode)))
      reject('access_denied', 'Código da sala inválido.');
    for (let attempt = 0; attempt < 8; attempt++) {
      const before = await this.store.read();
      const state = before ? JSON.parse(before) : { sessions: {}, revision: 0 };
      const events = [];
      const room = new VirtualServer({ name: 'OpenSpeak • Sala web', iceServers: [], idPrefix: `${secret()}-`,
        send: (clid, message) => events.push({ clid, message }) });
      room.restore(state.room);
      for (const [clid, entry] of Object.entries(state.sessions)) {
        if (entry.until < this.now()) {
          room.disconnect(Number(clid));
          delete state.sessions[clid];
        }
      }
      let current = { ...session }, data;
      if (cmd === 'login') {
        const entry = state.sessions[args.resumeClid];
        if (args.resumeToken) {
          if (!entry || !equal(digest(args.resumeToken), entry.tokenHash))
            reject('session_expired', 'A sessão expirou. Conecte novamente.');
          current = { clid: Number(args.resumeClid), token: args.resumeToken, lease: secret() };
          entry.lease = current.lease;
          entry.until = this.now() + SESSION_GRACE_MS;
          data = { ...room.view(current.clid), resumed: true };
        } else {
          if (room.clients.size >= 32) reject('room_full', 'Sala cheia (32 participantes).');
          current = { clid: room.connect(), token: secret(), lease: secret() };
          data = room.handle(current.clid, 'login', args);
          state.sessions[current.clid] = { tokenHash: digest(current.token), lease: current.lease,
            until: this.now() + SESSION_GRACE_MS };
        }
        data.resumeToken = current.token;
        data.tailscaleOnly = true;
      } else {
        const entry = state.sessions[current.clid];
        if (!entry || entry.lease !== current.lease || !equal(digest(current.token || ''), entry.tokenHash))
          reject('session_expired', 'A sessão expirou. Conecte novamente.');
        entry.until = this.now() + SESSION_GRACE_MS;
        if (cmd === 'logout') {
          room.disconnect(current.clid);
          delete state.sessions[current.clid];
          data = {};
        } else if (cmd === 'heartbeat' || cmd === 'sync') {
          data = cmd === 'sync' ? room.view(current.clid) : {};
        } else {
          data = room.handle(current.clid, cmd, args);
        }
      }
      state.room = room.snapshot();
      state.revision++;
      if (await this.store.commit(before, JSON.stringify(state), events))
        return { session: current, data };
    }
    reject('busy', 'Sala ocupada. Tente novamente.');
  }
}

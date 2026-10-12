import { EventEmitter } from './EventEmitter.js';
import { CommandError } from './ServerConnection.js';

// Function rotations only interrupt signaling: keep active media and resume
// the same participant ID with a short-lived, server-issued session token.
export class CloudConnection extends EventEmitter {
  constructor() {
    super();
    this.pending = new Map(); this.nextId = 1; this.stopped = false;
    this.delay = 1000; this.resume = null; this.auth = null; this.socket = null;
  }
  get connected() { return this.socket?.readyState === WebSocket.OPEN; }
  async connect(url) {
    this.url = url; this.stopped = false;
    await this.open();
  }
  open() {
    return new Promise((resolve, reject) => {
      const ws = this.socket = new WebSocket(this.url);
      let opened = false;
      const timer = setTimeout(() => { reject(new Error('Tempo esgotado ao conectar ao Vercel.')); ws.close(); }, 15_000);
      ws.addEventListener('open', async () => {
        clearTimeout(timer); opened = true;
        if (!this.resume) { resolve(); return; }
        try {
          let data;
          try { data = await this.sendCommand('login', { ...this.auth, resumeClid: this.resume.clid, resumeToken: this.resume.token }); }
          catch (err) {
            if (err.returnCode !== 'session_expired') throw err;
            data = await this.sendCommand('login', this.auth);
            data.restarted = true;
          }
          this.delay = 1000; this.emit('resumed', data); resolve();
        } catch (err) {
          reject(err);
          if (err.returnCode === 'access_denied') {
            this.disconnect();
          } else ws.close();
        }
      });
      ws.addEventListener('message', ev => {
        let msg; try { msg = JSON.parse(ev.data); } catch { return; }
        if (msg.type === 'notify') { this.emit('notify', msg.cmd, msg.args || {}); this.emit(msg.cmd, msg.args || {}); }
        if (msg.type !== 'res') return;
        const p = this.pending.get(msg.id); if (!p) return;
        this.pending.delete(msg.id); clearTimeout(p.timer);
        if (msg.return_code === 'ok') p.resolve(msg.data || {});
        else p.reject(new CommandError(p.cmd, msg.return_code, msg.error));
      });
      ws.addEventListener('close', () => {
        clearTimeout(timer);
        for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(new CommandError(p.cmd, 'reconnecting', 'Reconectando…')); }
        this.pending.clear();
        if (!opened) reject(new Error('Não foi possível conectar ao Vercel.'));
        if (this.stopped || !this.resume) return;
        this.emit('reconnecting');
        this.retry = setTimeout(() => this.open().catch(() => {}), this.delay);
        this.delay = Math.min(this.delay * 2, 10_000);
      });
    });
  }
  async sendCommand(cmd, args = {}) {
    if (!this.connected) throw new CommandError(cmd, 'reconnecting', 'Reconectando…');
    const id = this.nextId++;
    const result = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new CommandError(cmd, 'timeout')); }, 15_000);
      this.pending.set(id, { cmd, resolve, reject, timer });
      this.socket.send(JSON.stringify({ type: 'cmd', id, cmd, args }));
    });
    if (cmd === 'login') {
      this.auth = { nickname: args.nickname, uid: args.uid };
      this.resume = { clid: result.clid, token: result.resumeToken };
      clearInterval(this.heartbeat);
      this.heartbeat = setInterval(() => {
        if (this.connected) this.sendCommand('sync').then(data => this.emit('resumed', data)).catch(() => this.socket?.close());
      }, 30_000);
    }
    return result;
  }
  disconnect() {
    this.stopped = true;
    clearTimeout(this.retry); clearInterval(this.heartbeat);
    if (this.connected && this.resume) this.socket.send(JSON.stringify({ type: 'cmd', id: this.nextId++, cmd: 'logout', args: {} }));
    this.socket?.close(1000, 'bye');
    this.resume = null; this.auth = null;
    this.emit('disconnected', { reason: 'bye' });
  }
}

// Conexão de comando com o servidor (equivalente ao canal de comandos do TS).
// Comandos retornam Promise com `data`; erros carregam `return_code`.
//
// Servidor que devolve `resumeToken` no login (o nativo) deixa retomar a sessão:
// se a conexão cai, reconectamos e entramos de novo no mesmo clid, sem sair da
// sala nem derrubar a mídia ('reconnecting' -> 'resumed').

import { EventEmitter } from './EventEmitter.js';

export class CommandError extends Error {
  constructor(cmd, returnCode, message) {
    super(message || returnCode);
    this.cmd = cmd;
    this.returnCode = returnCode;
  }
}

const COMMAND_TIMEOUT_MS = 15_000;
// O navegador não manda ping de WebSocket: um comando leve faz o papel. Sem
// resposta, a rede sumiu sem fechar o TCP; trocar de socket é mais rápido.
const LIVENESS_INTERVAL_MS = 5_000;
const LIVENESS_TIMEOUT_MS = 10_000;
const MAX_RETRY_MS = 10_000;

export class ServerConnection extends EventEmitter {
  #ws = null;
  #url = '';
  #nextId = 1;
  #pending = new Map();
  #resume = null; // { clid, token, auth }
  #stopped = false;
  #retryMs = 1000;
  #retryTimer = null;
  #liveness = null;

  get connected() {
    return this.#ws?.readyState === WebSocket.OPEN;
  }

  connect(url) {
    this.#url = url;
    this.#stopped = false;
    this.#resume = null;
    return new Promise((resolve, reject) => this.#open(resolve, reject));
  }

  /** Abre um socket. Sem `resolve`, é uma reconexão: retoma a sessão ao abrir. */
  #open(resolve, reject) {
    const ws = new WebSocket(this.#url);
    ws.binaryType = 'arraybuffer';
    this.#ws = ws;
    let opened = false;
    const connectTimer = setTimeout(() => {
      if (opened) return;
      reject?.(new Error('Não foi possível conectar. Confira o Tailscale, o servidor e a permissão de acesso à rede local do navegador.'));
      reject = null;
      this.#lost(ws, { wasOpen: false });
    }, COMMAND_TIMEOUT_MS);
    ws.addEventListener('open', () => {
      if (ws !== this.#ws) return;
      clearTimeout(connectTimer);
      opened = true;
      if (resolve) resolve();
      else this.#resumeSession();
    });
    ws.addEventListener('message', ev => { if (ws === this.#ws) this.#onMessage(ev.data); });
    ws.addEventListener('close', ev => {
      clearTimeout(connectTimer);
      if (!opened) reject?.(new Error('não foi possível conectar ao servidor'));
      this.#lost(ws, { wasOpen: opened, code: ev.code, reason: ev.reason });
    });
  }

  async #resumeSession() {
    const ws = this.#ws;
    const { clid, token, auth } = this.#resume;
    let data;
    try {
      try {
        data = await this.sendCommand('login', { ...auth, resumeClid: clid, resumeToken: token });
      } catch (err) {
        if (err.returnCode !== 'session_expired') throw err;
        // A espera do servidor acabou: entra de novo como cliente novo.
        data = await this.sendCommand('login', auth);
        data.restarted = true;
      }
    } catch {
      this.#lost(ws, { wasOpen: true });
      return;
    }
    this.#retryMs = 1000;
    this.emit('resumed', data);
  }

  /** Socket encerrado (ou dado como morto): reconecta se a sessão é retomável. */
  #lost(ws, { wasOpen, code, reason }) {
    if (ws !== this.#ws) return;
    this.#ws = null;
    try { ws.close(); } catch { /* já fechado */ }
    const retrying = !this.#stopped && !!this.#resume;
    for (const { reject, timer, cmd } of this.#pending.values()) {
      clearTimeout(timer);
      reject(retrying ? new CommandError(cmd, 'reconnecting', 'Reconectando…') : new CommandError(cmd, 'disconnected', 'conexão encerrada'));
    }
    this.#pending.clear();
    if (!retrying) {
      clearInterval(this.#liveness);
      if (wasOpen) this.emit('disconnected', { code, reason });
      return;
    }
    this.emit('reconnecting');
    clearTimeout(this.#retryTimer);
    this.#retryTimer = setTimeout(() => { if (!this.#stopped) this.#open(); }, this.#retryMs);
    this.#retryMs = Math.min(this.#retryMs * 2, MAX_RETRY_MS);
  }

  disconnect() {
    const wasResumable = !!this.#resume, wasConnected = this.connected;
    this.#stopped = true;
    clearTimeout(this.#retryTimer);
    clearInterval(this.#liveness);
    if (wasConnected && wasResumable) this.#ws.send(JSON.stringify({ type: 'cmd', id: this.#nextId++, cmd: 'logout', args: {} }));
    this.#ws?.close(1000, 'bye');
    // Reconectando não há socket aberto, e sem ele nenhum 'close' avisa a interface.
    if (!wasConnected && wasResumable) {
      this.#ws = null;
      this.emit('disconnected', { code: 1000, reason: 'bye' });
    }
    this.#resume = null;
  }

  sendCommand(cmd, args = {}) {
    if (!this.connected) {
      const code = this.#resume && !this.#stopped ? 'reconnecting' : 'disconnected';
      return Promise.reject(new CommandError(cmd, code, code === 'reconnecting' ? 'Reconectando…' : 'sem conexão'));
    }
    const fresh = cmd === 'login' && !args.resumeToken;
    if (cmd === 'login') args = { ...args, resumable: true };
    const id = this.#nextId++;
    const result = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.#pending.delete(id);
        reject(new CommandError(cmd, 'timeout', `tempo esgotado em ${cmd}`));
      }, cmd === 'heartbeat' ? LIVENESS_TIMEOUT_MS : COMMAND_TIMEOUT_MS);
      this.#pending.set(id, { resolve, reject, timer, cmd });
      this.#ws.send(JSON.stringify({ type: 'cmd', id, cmd, args }));
    });
    if (fresh) result.then(data => this.#onLogin(args, data), () => {});
    return result;
  }

  #onLogin(args, data) {
    if (!data.resumeToken) return; // servidor sem retomada (P2P em Node)
    const { resumable, resumeClid, resumeToken, ...auth } = args;
    this.#resume = { clid: data.clid, token: data.resumeToken, auth };
    clearInterval(this.#liveness);
    this.#liveness = setInterval(() => {
      const ws = this.#ws;
      if (!ws || ws.readyState !== WebSocket.OPEN) return;
      this.sendCommand('heartbeat').catch(err => { if (err.returnCode === 'timeout') this.#lost(ws, { wasOpen: true }); });
    }, LIVENESS_INTERVAL_MS);
  }

  #onMessage(raw) {
    // Binário = mídia do relay para espectadores por WebSocket (atividade do Discord).
    if (typeof raw !== 'string') return this.emit('binary', raw);
    let msg;
    try {
      msg = JSON.parse(raw);
    } catch {
      return;
    }
    if (msg.type === 'res') {
      const pending = this.#pending.get(msg.id);
      if (!pending) return;
      this.#pending.delete(msg.id);
      clearTimeout(pending.timer);
      if (msg.return_code === 'ok') pending.resolve(msg.data ?? {});
      else pending.reject(new CommandError(pending.cmd, msg.return_code, msg.error));
    } else if (msg.type === 'notify') {
      this.emit('notify', msg.cmd, msg.args ?? {});
      this.emit(msg.cmd, msg.args ?? {});
    }
  }
}

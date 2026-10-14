// Conexão de comando com o servidor (equivalente ao canal de comandos do TS).
// Comandos retornam Promise com `data`; erros carregam `return_code`.

import { EventEmitter } from './EventEmitter.js';

export class CommandError extends Error {
  constructor(cmd, returnCode, message) {
    super(message || returnCode);
    this.cmd = cmd;
    this.returnCode = returnCode;
  }
}

const COMMAND_TIMEOUT_MS = 15_000;

export class ServerConnection extends EventEmitter {
  #ws = null;
  #nextId = 1;
  #pending = new Map();

  get connected() {
    return this.#ws?.readyState === WebSocket.OPEN;
  }

  connect(url) {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(url);
      ws.binaryType = 'arraybuffer';
      this.#ws = ws;
      let opened = false;
      const connectTimer = setTimeout(() => {
        if (opened) return;
        reject(new Error('Não foi possível conectar. Confira o Tailscale, o servidor e a permissão de acesso à rede local do navegador.'));
        ws.close();
      }, COMMAND_TIMEOUT_MS);
      ws.addEventListener('open', () => {
        clearTimeout(connectTimer);
        opened = true;
        resolve();
      });
      ws.addEventListener('message', ev => this.#onMessage(ev.data));
      ws.addEventListener('close', ev => {
        clearTimeout(connectTimer);
        for (const { reject: rej, timer, cmd } of this.#pending.values()) {
          clearTimeout(timer);
          rej(new CommandError(cmd, 'disconnected', 'conexão encerrada'));
        }
        this.#pending.clear();
        if (!opened) reject(new Error('não foi possível conectar ao servidor'));
        else this.emit('disconnected', { code: ev.code, reason: ev.reason });
      });
    });
  }

  disconnect() {
    this.#ws?.close(1000, 'bye');
  }

  sendCommand(cmd, args = {}) {
    if (!this.connected) return Promise.reject(new CommandError(cmd, 'disconnected', 'sem conexão'));
    const id = this.#nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.#pending.delete(id);
        reject(new CommandError(cmd, 'timeout', `tempo esgotado em ${cmd}`));
      }, COMMAND_TIMEOUT_MS);
      this.#pending.set(id, { resolve, reject, timer, cmd });
      this.#ws.send(JSON.stringify({ type: 'cmd', id, cmd, args }));
    });
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

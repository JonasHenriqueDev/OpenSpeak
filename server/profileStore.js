// Perfis: um apelido vira perfil ao ganhar uma senha. Os dados ficam num JSON no
// servidor, no mesmo formato de native/server/ProfileStore.cpp.

import { pbkdf2Sync, randomBytes, timingSafeEqual } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

// Baixo de propósito: o login roda na thread da sinalização e a VM de produção tem 1/8 de vCPU.
const ITERATIONS = 50_000;
const MAX_FAILURES = 5;
const LOCK_MS = 60_000;

export const profileKey = nickname => nickname.trim().toLowerCase();

const pbkdf2 = (password, salt, iterations) => pbkdf2Sync(password, salt, iterations, 32, 'sha256');

export function hashPassword(password, salt = randomBytes(16), iterations = ITERATIONS) {
  return `pbkdf2-sha256$${iterations}$${salt.toString('base64')}$${pbkdf2(password, salt, iterations).toString('base64')}`;
}

export function checkPassword(password, stored) {
  const [scheme, iterations, salt, hash] = String(stored).split('$');
  if (scheme !== 'pbkdf2-sha256' || !(Number(iterations) > 0)) return false;
  const expected = Buffer.from(hash ?? '', 'base64');
  const actual = pbkdf2(password, Buffer.from(salt ?? '', 'base64'), Number(iterations));
  return expected.length === actual.length && timingSafeEqual(expected, actual);
}

export class ProfileStore {
  /** @param {string|null} file sem arquivo, os perfis vivem só na memória */
  constructor(file = null, now = Date.now) {
    this.file = file;
    this.now = now;
    this.profiles = new Map();
    this.failures = new Map(); // key -> { count, lockedUntil }
    if (file && fs.existsSync(file)) {
      const data = JSON.parse(fs.readFileSync(file, 'utf8'));
      this.profiles = new Map(Object.entries(data.profiles ?? {}));
    }
  }

  get(nickname) {
    return this.profiles.get(profileKey(nickname)) ?? null;
  }

  claim(nickname, password) {
    const key = profileKey(nickname);
    this.profiles.set(key, { nickname, password: hashPassword(password), displayName: '', color: '', description: '', avatar: '' });
    this.#save();
    return key;
  }

  setPassword(key, password) {
    this.profiles.get(key).password = hashPassword(password);
    this.#save();
  }

  update(key, fields) {
    Object.assign(this.profiles.get(key), fields);
    this.#save();
  }

  /** 'ok', 'wrong' ou 'locked' (tentativas demais: segura quem tenta adivinhar a senha). */
  verify(key, password) {
    const failure = this.failures.get(key);
    if (failure?.lockedUntil > this.now()) return 'locked';
    if (checkPassword(password, this.profiles.get(key).password)) {
      this.failures.delete(key);
      return 'ok';
    }
    const count = (failure?.count ?? 0) + 1;
    this.failures.set(key, count >= MAX_FAILURES ? { count: 0, lockedUntil: this.now() + LOCK_MS } : { count, lockedUntil: 0 });
    return 'wrong';
  }

  #save() {
    if (!this.file) return;
    try {
      fs.mkdirSync(path.dirname(this.file), { recursive: true });
      const temp = `${this.file}.tmp`;
      fs.writeFileSync(temp, JSON.stringify({ version: 1, profiles: Object.fromEntries(this.profiles) }), { mode: 0o600 });
      fs.renameSync(temp, this.file);
    } catch (err) {
      console.warn('[openspeak] não foi possível gravar os perfis:', err.message);
    }
  }
}

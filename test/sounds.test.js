import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from '../public/js/core/EventEmitter.js';

const played = [];
globalThis.Audio = class {
  constructor(src) { this.src = src; this.volume = 1; this.currentTime = 0; }
  play() { played.push(this.src.replace(/^sounds\//, '').replace(/\.wav$/, '')); return Promise.resolve(); }
};
const { SoundEffects } = await import('../public/js/ui/sounds.js');

function setup(values = {}) {
  const data = { soundEffects: true, soundTheme: 'suave', soundVolume: 60, ...values };
  const settings = { get: k => data[k], set: (k, v) => { data[k] = v; } };
  const handler = new EventEmitter();
  Object.assign(handler, { ownClid: 1, clients: new Map(), streams: new Map() });
  Object.defineProperty(handler, 'ownClient', { get() { return this.clients.get(this.ownClid) ?? null; } });
  const sounds = new SoundEffects(handler, settings);
  const tick = () => { sounds.refresh(); return played.splice(0).at(-1) ?? null; };
  return { handler, settings, tick };
}

test('entrar/sair do canal e pessoas entrando/saindo', () => {
  played.length = 0;
  const { handler, tick } = setup();
  handler.clients.set(1, { clid: 1, cid: 10 });
  assert.equal(tick(), 'suave/entrar_sala');
  handler.clients.set(2, { clid: 2, cid: 10 });
  assert.equal(tick(), 'suave/alguem_entrou');
  handler.clients.set(3, { clid: 3, cid: 20 }); // outro canal: silêncio
  assert.equal(tick(), null);
  handler.clients.get(2).cid = 20;
  assert.equal(tick(), 'suave/alguem_saiu');
  handler.clients.get(1).cid = 20; // mudei de canal: só "entrar", sem "alguém entrou"
  assert.equal(tick(), 'suave/entrar_sala');
  handler.emit('disconnected', {});
  assert.equal(played.splice(0).at(-1), 'suave/sair_sala');
});

test('transmissões: iniciar, pausar, retomar e encerrar', () => {
  played.length = 0;
  const { handler, tick } = setup({ soundTheme: 'bolha' });
  handler.clients.set(1, { clid: 1, cid: 10 });
  handler.streams.set('s', { streamIdentifier: 's', cid: 99, paused: false }); // canal alheio
  tick();
  handler.streams.set('a', { streamIdentifier: 'a', cid: 10, paused: false });
  assert.equal(tick(), 'bolha/iniciar_transmissao');
  handler.streams.get('a').paused = true;
  assert.equal(tick(), 'bolha/pausar_transmissao');
  handler.streams.get('a').paused = false;
  assert.equal(tick(), 'bolha/iniciar_transmissao');
  handler.streams.delete('a');
  assert.equal(tick(), 'bolha/pausar_transmissao');
});

test('quem entra já transmitindo toca só "entrou"; mudo não toca nada', () => {
  played.length = 0;
  const { handler, settings, tick } = setup();
  handler.clients.set(1, { clid: 1, cid: 10 });
  tick();
  handler.clients.set(2, { clid: 2, cid: 10 });
  handler.streams.set('b', { streamIdentifier: 'b', cid: 10, paused: false });
  assert.equal(tick(), 'suave/alguem_entrou');
  settings.set('soundEffects', false);
  handler.clients.delete(2);
  assert.equal(tick(), null);
});

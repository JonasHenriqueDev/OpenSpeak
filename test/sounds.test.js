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

test('espectadores entrando e saindo da minha transmissão', () => {
  played.length = 0;
  const { handler, tick } = setup();
  handler.clients.set(1, { clid: 1, cid: 10 });
  handler.streams.set('m', { streamIdentifier: 'm', cid: 10, ownerClid: 1, paused: false, viewerCount: 0 });
  assert.equal(tick(), 'suave/entrar_sala');
  handler.streams.get('m').viewerCount = 1;
  assert.equal(tick(), 'suave/espectador_entrou');
  handler.streams.get('m').viewerCount = 0;
  assert.equal(tick(), 'suave/espectador_saiu');
  handler.streams.set('o', { streamIdentifier: 'o', cid: 10, ownerClid: 2, paused: false, viewerCount: 0 });
  tick();
  handler.streams.get('o').viewerCount = 3; // stream de outra pessoa: silêncio
  assert.equal(tick(), null);
});

test('espectador ouve quem entra e sai do stream que está assistindo (e a própria entrada/saída)', () => {
  played.length = 0;
  const data = { soundEffects: true, soundTheme: 'suave', soundVolume: 60 };
  const settings = { get: k => data[k], set: (k, v) => { data[k] = v; } };
  const handler = new EventEmitter();
  Object.assign(handler, { ownClid: 1, clients: new Map(), streams: new Map() });
  Object.defineProperty(handler, 'ownClient', { get() { return this.clients.get(this.ownClid) ?? null; } });
  const manager = new EventEmitter();
  let watching = [];
  manager.watchedStreams = () => watching;
  const sounds = new SoundEffects(handler, settings, manager);
  const tick = () => { sounds.refresh(); return played.splice(0).at(-1) ?? null; };

  handler.clients.set(1, { clid: 1, cid: 10 });
  handler.streams.set('x', { streamIdentifier: 'x', cid: 20, ownerClid: 2, paused: false, viewerCount: 1 });
  tick();
  handler.streams.get('x').viewerCount = 2; // alguém entrou, mas não estou assistindo
  assert.equal(tick(), null);
  watching = ['x']; // pedi para assistir: base
  assert.equal(tick(), null);
  handler.streams.get('x').viewerCount = 3; // o servidor me contou
  assert.equal(tick(), 'suave/espectador_entrou');
  handler.streams.get('x').viewerCount = 4;
  assert.equal(tick(), 'suave/espectador_entrou');
  handler.streams.get('x').viewerCount = 3;
  assert.equal(tick(), 'suave/espectador_saiu');
  watching = []; // saí
  assert.equal(tick(), 'suave/espectador_saiu');
  handler.streams.get('x').viewerCount = 2;
  assert.equal(tick(), null);
});

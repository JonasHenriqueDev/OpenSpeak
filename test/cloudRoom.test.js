import test from 'node:test';
import assert from 'node:assert/strict';
import { CloudRoom, SESSION_GRACE_MS } from '../server/cloudRoom.js';

function storage() {
  return { value: null, events: [], async read() { return this.value; },
    async commit(before, after, events) {
      if (before !== this.value) return false;
      this.value = after; this.events.push(...events); return true;
    } };
}
const login = (room, name) => room.execute({}, 'login', { nickname: name, uid: name, accessCode: 'private-room-code' });

test('expired Redis room cannot reuse an earlier stream identifier', async () => {
  const store = storage(), room = new CloudRoom(store, 'private-room-code');
  const first = await login(room, 'Alice');
  const oldStream = await room.execute(first.session, 'setupstream', {});
  store.value = null;
  const second = await login(room, 'Alice');
  const newStream = await room.execute(second.session, 'setupstream', {});
  assert.notEqual(oldStream.data.streamIdentifier, newStream.data.streamIdentifier);
  await assert.rejects(room.execute({}, 'login', { accessCode: 'private-room-code', resumeClid: first.session.clid, resumeToken: first.data.resumeToken }), { code: 'session_expired' });
});

test('cloud rejects unauthenticated login and commands without exposing the room', async () => {
  const store = storage(), room = new CloudRoom(store, 'private-room-code');
  await assert.rejects(room.execute({}, 'login', { accessCode: 'wrong' }), { code: 'access_denied' });
  assert.equal(store.value, null);
  await assert.rejects(room.execute({}, 'sync'), { code: 'session_expired' });
});

test('two instances share clients and simultaneous writes do not lose streams', async () => {
  const store = storage(), a = new CloudRoom(store, 'private-room-code'), b = new CloudRoom(store, 'private-room-code');
  const [alice, bob] = await Promise.all([login(a, 'Alice'), login(b, 'Bob')]);
  assert.notEqual(alice.session.clid, bob.session.clid);
  const [x, y] = await Promise.all([a.execute(alice.session, 'setupstream', { name: 'A' }), b.execute(bob.session, 'setupstream', { name: 'B' })]);
  assert.notEqual(x.data.streamIdentifier, y.data.streamIdentifier);
  const view = await a.execute(alice.session, 'sync');
  assert.equal(view.data.clients.length, 2); assert.equal(view.data.streams.length, 2);
  assert.deepEqual(view.data.iceServers, []);
});

test('resume keeps media membership, fences the old socket, logout removes the stream', async () => {
  const store = storage(), room = new CloudRoom(store, 'private-room-code');
  const alice = await login(room, 'Alice'), bob = await login(room, 'Bob');
  const stream = await room.execute(alice.session, 'setupstream', { name: 'A' });
  const id = stream.data.streamIdentifier;
  await room.execute(bob.session, 'joinstreamrequest', { streamIdentifier: id, codecs: ['H264'] });
  await room.execute(alice.session, 'respondjoinstreamrequest', { streamIdentifier: id, clid: bob.session.clid, accept: true });
  const resumed = await room.execute({}, 'login', { accessCode: 'private-room-code', resumeClid: alice.session.clid, resumeToken: alice.data.resumeToken });
  assert.equal(resumed.data.clid, alice.session.clid);
  assert.equal(resumed.data.streams[0].viewerCount, 1);
  await assert.rejects(room.execute(alice.session, 'logout'), { code: 'session_expired' });
  await room.execute(resumed.session, 'logout');
  assert.equal((await room.execute(bob.session, 'sync')).data.streams.length, 0);
});

test('abandoned clients expire but live participants remain', async () => {
  let now = 1000;
  const store = storage(), room = new CloudRoom(store, 'private-room-code', () => now);
  const alice = await login(room, 'Alice'), bob = await login(room, 'Bob');
  await room.execute(alice.session, 'setupstream', {});
  now += SESSION_GRACE_MS - 1000;
  await room.execute(bob.session, 'heartbeat');
  now += 2000;
  const view = await room.execute(bob.session, 'sync');
  assert.equal(view.data.clients.length, 1); assert.equal(view.data.streams.length, 0);
  await assert.rejects(room.execute({}, 'login', { accessCode: 'private-room-code', resumeClid: alice.session.clid, resumeToken: alice.data.resumeToken }), { code: 'session_expired' });
});

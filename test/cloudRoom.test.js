import test from 'node:test';
import assert from 'node:assert/strict';
import { CloudRoom, SESSION_GRACE_MS, KICK_COOLDOWN_MS } from '../server/cloudRoom.js';

function storage() {
  return { value: null, events: [], async read() { return this.value; },
    async commit(before, after, events) {
      if (before !== this.value) return false;
      this.value = after; this.events.push(...events); return true;
    },
    policy: null, async readPolicy() { return this.policy; },
    async commitPolicy(before, after) {
      if ((before || null) !== this.policy) return false;
      this.policy = after; return true;
    } };
}
const login = (room, name) => room.execute({}, 'login', { nickname: name, uid: name });

test('expired Redis room cannot reuse an earlier stream identifier', async () => {
  const store = storage(), room = new CloudRoom(store);
  const first = await login(room, 'Alice');
  const oldStream = await room.execute(first.session, 'setupstream', {});
  store.value = null;
  const second = await login(room, 'Alice');
  const newStream = await room.execute(second.session, 'setupstream', {});
  assert.notEqual(oldStream.data.streamIdentifier, newStream.data.streamIdentifier);
  await assert.rejects(room.execute({}, 'login', { resumeClid: first.session.clid, resumeToken: first.data.resumeToken }), { code: 'session_expired' });
});

test('public cloud admits login without a room code but still requires a valid session for commands', async () => {
  const store = storage(), room = new CloudRoom(store);
  const result = await login(room, 'Guest');
  assert.ok(result.data.resumeToken);
  assert.equal(result.data.clients[0].nickname, 'Guest');
  await assert.rejects(room.execute({}, 'sync'), { code: 'session_expired' });
  await assert.rejects(room.execute({}, 'login', { resumeClid: result.data.clid, resumeToken: 'forged' }), { code: 'session_expired' });
});

test('two instances share clients and simultaneous writes do not lose streams', async () => {
  const store = storage(), a = new CloudRoom(store), b = new CloudRoom(store);
  const [alice, bob] = await Promise.all([login(a, 'Alice'), login(b, 'Bob')]);
  assert.notEqual(alice.session.clid, bob.session.clid);
  const [x, y] = await Promise.all([a.execute(alice.session, 'setupstream', { name: 'A' }), b.execute(bob.session, 'setupstream', { name: 'B' })]);
  assert.notEqual(x.data.streamIdentifier, y.data.streamIdentifier);
  const view = await a.execute(alice.session, 'sync');
  assert.equal(view.data.clients.length, 2); assert.equal(view.data.streams.length, 2);
  assert.deepEqual(view.data.iceServers, []);
});

test('resume keeps media membership, fences the old socket, logout removes the stream', async () => {
  const store = storage(), room = new CloudRoom(store);
  const alice = await login(room, 'Alice'), bob = await login(room, 'Bob');
  const stream = await room.execute(alice.session, 'setupstream', { name: 'A' });
  const id = stream.data.streamIdentifier;
  await room.execute(bob.session, 'joinstreamrequest', { streamIdentifier: id, codecs: ['H264'] });
  await room.execute(alice.session, 'respondjoinstreamrequest', { streamIdentifier: id, clid: bob.session.clid, accept: true });
  const resumed = await room.execute({}, 'login', { resumeClid: alice.session.clid, resumeToken: alice.data.resumeToken });
  assert.equal(resumed.data.clid, alice.session.clid);
  assert.equal(resumed.data.streams[0].viewerCount, 1);
  await assert.rejects(room.execute(alice.session, 'logout'), { code: 'session_expired' });
  await room.execute(resumed.session, 'logout');
  assert.equal((await room.execute(bob.session, 'sync')).data.streams.length, 0);
});

test('abandoned clients expire but live participants remain', async () => {
  let now = 1000;
  const store = storage(), room = new CloudRoom(store, () => now);
  const alice = await login(room, 'Alice'), bob = await login(room, 'Bob');
  await room.execute(alice.session, 'setupstream', {});
  now += SESSION_GRACE_MS - 1000;
  await room.execute(bob.session, 'heartbeat');
  now += 2000;
  const view = await room.execute(bob.session, 'sync');
  assert.equal(view.data.clients.length, 1); assert.equal(view.data.streams.length, 0);
  await assert.rejects(room.execute({}, 'login', { resumeClid: alice.session.clid, resumeToken: alice.data.resumeToken }), { code: 'session_expired' });
});

test('admin kick disconnects, closes the socket and refuses the automatic re-login briefly', async () => {
  let now = 1000;
  const store = storage(), room = new CloudRoom(store, () => now);
  const alice = await login(room, 'Alice'), bob = await login(room, 'Bob');
  store.events.length = 0;
  await room.kick(bob.session.clid);
  assert.ok(store.events.some(e => e.clid === bob.session.clid && e.message.type === 'close'));
  assert.ok(store.events.some(e => e.clid === alice.session.clid && e.message.cmd === 'notifyclientleftview'));
  await assert.rejects(room.execute(bob.session, 'sync'), { code: 'session_expired' });
  await assert.rejects(login(room, 'Bob'), { code: 'access_denied' });
  now += KICK_COOLDOWN_MS + 1;
  assert.ok((await login(room, 'Bob')).data.clid);
  assert.equal((await room.status()).bans.length, 0);
});

test('ban matches the client id or the IP address and can be lifted', async () => {
  let now = 1000;
  const store = storage(), room = new CloudRoom(store, () => now);
  const bob = await room.execute({}, 'login', { nickname: 'Bob', uid: 'bob' }, { ip: '10.0.0.5' });
  await room.kick(bob.session.clid, { minutes: 60, reason: 'spam' });
  await assert.rejects(room.execute({}, 'login', { nickname: 'Bob2', uid: 'other' }, { ip: '10.0.0.5' }), { code: 'access_denied' });
  await assert.rejects(room.execute({}, 'login', { nickname: 'Bob', uid: 'bob' }, { ip: '10.9.9.9' }), { code: 'access_denied' });
  const [ban] = (await room.status()).bans;
  assert.equal(ban.reason, 'spam');
  await room.unban(ban.id);
  assert.ok((await room.execute({}, 'login', { nickname: 'Bob', uid: 'bob' }, { ip: '10.0.0.5' })).data.clid);
});

test('locked room keeps members and resumes but refuses newcomers; bans survive the room key expiring', async () => {
  const store = storage(), room = new CloudRoom(store);
  const alice = await login(room, 'Alice');
  await room.setLocked(true);
  await assert.rejects(login(room, 'Bob'), { code: 'access_denied' });
  const resumed = await room.execute({}, 'login', { resumeClid: alice.session.clid, resumeToken: alice.data.resumeToken });
  assert.equal(resumed.data.clid, alice.session.clid);
  store.value = null; // Redis EX on the idle room
  await assert.rejects(login(room, 'Bob'), { code: 'access_denied' });
  await room.setLocked(false);
  assert.ok((await login(room, 'Bob')).data.clid);
});

test('reset closes every socket, clears streams and never reuses a client id', async () => {
  const store = storage(), room = new CloudRoom(store);
  const alice = await login(room, 'Alice'), bob = await login(room, 'Bob');
  await room.execute(alice.session, 'setupstream', {});
  await room.setLocked(true);
  store.events.length = 0;
  assert.equal((await room.reset()).disconnected, 2);
  assert.equal(store.events.filter(e => e.message.type === 'close').length, 2);
  const status = await room.status();
  assert.equal(status.clients.length, 0); assert.equal(status.streams, 0); assert.equal(status.locked, true);
  await room.setLocked(false);
  const carol = await login(room, 'Carol');
  assert.ok(carol.session.clid > bob.session.clid);
});

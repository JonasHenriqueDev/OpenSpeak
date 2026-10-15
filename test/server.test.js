import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VirtualServer } from '../server/virtualServer.js';
import { StreamAccess, LeaveReason } from '../server/protocol.js';

function setup() {
  const inbox = new Map();
  const server = new VirtualServer({ send: (clid, msg) => inbox.get(clid)?.push(msg) });
  const login = nickname => {
    const clid = server.connect();
    inbox.set(clid, []);
    server.handle(clid, 'login', { nickname, uid: `uid-${nickname}` });
    return clid;
  };
  const notes = (clid, cmd) => inbox.get(clid).filter(m => m.cmd === cmd);
  const clear = () => inbox.forEach(list => { list.length = 0; });
  return { server, login, notes, clear };
}

test('server broadcasts measured client ping and includes it in new login views', () => {
  const { server, login, notes } = setup();
  const a = login('a'), b = login('b');
  server.setClientLatency(a, 42);
  const sample = notes(b, 'notifyclientlatency').at(-1).args;
  assert.equal(sample.clid, a);
  assert.equal(sample.pingMs, 42);
  assert.ok(sample.pingUpdatedAt > 0);
  assert.equal(sample.ip, undefined);
  assert.equal(server.view(b).clients.find(c => c.clid === a).pingMs, 42);
  server.setClientLatency(a, -1);
  assert.equal(notes(b, 'notifyclientlatency').at(-1).args.pingMs, null);
  assert.throws(() => server.handle(a, 'setClientLatency', {pingMs:0}));
});

test('fluxo completo: setup, pedido, aceite, sinalização e saída', () => {
  const { server, login, notes } = setup();
  const host = login('host');
  const viewer = login('viewer');

  const { streamIdentifier } = server.handle(host, 'setupstream', { name: 'Tela', access: StreamAccess.PRIVATE });
  assert.equal(notes(viewer, 'notifystreamstarted').length, 1);

  server.handle(viewer, 'joinstreamrequest', { streamIdentifier, codecs: ['video/VP9'] });
  const [req] = notes(host, 'notifyjoinstreamrequest');
  assert.equal(req.args.clid, viewer);
  assert.deepEqual(req.args.codecs, ['video/VP9']);

  // antes de aceitar, a sinalização é recusada
  assert.throws(() => server.handle(host, 'streamsignaling', { streamIdentifier, clid: viewer, payload: { type: 'offer', sdp: 'x' } }),
    { code: 'permission_denied' });

  server.handle(host, 'respondjoinstreamrequest', { streamIdentifier, clid: viewer, accept: true });
  assert.equal(notes(viewer, 'notifyrespondjoinstreamrequest')[0].args.accepted, true);
  assert.equal(notes(host, 'notifystreamclientjoined')[0].args.clid, viewer);

  server.handle(host, 'streamsignaling', { streamIdentifier, clid: viewer, payload: { type: 'offer', sdp: 'v=0' } });
  const [sig] = notes(viewer, 'notifystreamsignaling');
  assert.equal(sig.args.clid, host);
  assert.equal(sig.args.payload.type, 'offer');

  server.handle(viewer, 'streamsignaling', { streamIdentifier, clid: host, payload: { type: 'answer', sdp: 'v=0' } });
  assert.equal(notes(host, 'notifystreamsignaling')[0].args.payload.type, 'answer');

  server.handle(viewer, 'removeclientfromstream', { streamIdentifier });
  assert.equal(notes(host, 'notifystreamclientleft')[0].args.reason, LeaveReason.LEFT);
});

test('ban impede novo pedido e pode ser desfeito', () => {
  const { server, login, notes } = setup();
  const host = login('host');
  const viewer = login('viewer');
  const { streamIdentifier } = server.handle(host, 'setupstream', {});
  server.handle(viewer, 'joinstreamrequest', { streamIdentifier });
  server.handle(host, 'respondjoinstreamrequest', { streamIdentifier, clid: viewer, accept: false, ban: true });
  assert.equal(notes(viewer, 'notifyrespondjoinstreamrequest')[0].args.reason, LeaveReason.BANNED);
  assert.throws(() => server.handle(viewer, 'joinstreamrequest', { streamIdentifier }), { code: 'banned' });
  server.handle(host, 'streamunban', { streamIdentifier, uid: 'uid-viewer' });
  assert.doesNotThrow(() => server.handle(viewer, 'joinstreamrequest', { streamIdentifier }));
});

test('kick com ban remove espectador e notifica os dois lados', () => {
  const { server, login, notes } = setup();
  const host = login('host');
  const viewer = login('viewer');
  const { streamIdentifier } = server.handle(host, 'setupstream', {});
  server.handle(viewer, 'joinstreamrequest', { streamIdentifier });
  server.handle(host, 'respondjoinstreamrequest', { streamIdentifier, clid: viewer, accept: true });
  server.handle(host, 'removeclientfromstream', { streamIdentifier, clid: viewer, reason: LeaveReason.BANNED });
  assert.equal(notes(viewer, 'notifystreamclientleft')[0].args.reason, LeaveReason.BANNED);
  assert.equal(notes(host, 'notifystreambanadded').length, 1);
  assert.throws(() => server.handle(viewer, 'joinstreamrequest', { streamIdentifier }), { code: 'banned' });
});

test('espectador não pode remover outros', () => {
  const { server, login } = setup();
  const host = login('host');
  const a = login('a');
  const b = login('b');
  const { streamIdentifier } = server.handle(host, 'setupstream', {});
  for (const c of [a, b]) {
    server.handle(c, 'joinstreamrequest', { streamIdentifier });
    server.handle(host, 'respondjoinstreamrequest', { streamIdentifier, clid: c, accept: true });
  }
  assert.throws(() => server.handle(a, 'removeclientfromstream', { streamIdentifier, clid: b }), { code: 'permission_denied' });
  // espectador também não conversa com outro espectador
  assert.throws(() => server.handle(a, 'streamsignaling', { streamIdentifier, clid: b, payload: { type: 'ice' } }), { code: 'permission_denied' });
});

test('limite de espectadores', () => {
  const { server, login } = setup();
  const host = login('host');
  const a = login('a');
  const b = login('b');
  const { streamIdentifier } = server.handle(host, 'setupstream', { viewerLimit: 1 });
  server.handle(a, 'joinstreamrequest', { streamIdentifier });
  server.handle(b, 'joinstreamrequest', { streamIdentifier });
  server.handle(host, 'respondjoinstreamrequest', { streamIdentifier, clid: a, accept: true });
  assert.throws(() => server.handle(host, 'respondjoinstreamrequest', { streamIdentifier, clid: b, accept: true }), { code: 'viewer_limit' });
});

test('sair do canal encerra os próprios streams; desconectar idem', () => {
  const { server, login, notes } = setup();
  const host = login('host');
  const viewer = login('viewer');
  const s1 = server.handle(host, 'setupstream', {}).streamIdentifier;
  server.handle(host, 'clientmove', { cid: 2 });
  assert.equal(notes(viewer, 'notifystreamstopped')[0].args.streamIdentifier, s1);
  const s2 = server.handle(host, 'setupstream', {}).streamIdentifier;
  server.disconnect(host);
  assert.equal(notes(viewer, 'notifystreamstopped')[1].args.streamIdentifier, s2);
  assert.equal(server.streams.size, 0);
});

test('pedido exige estar no mesmo canal', () => {
  const { server, login } = setup();
  const host = login('host');
  const viewer = login('viewer');
  const { streamIdentifier } = server.handle(host, 'setupstream', {});
  server.handle(viewer, 'clientmove', { cid: 3 });
  assert.throws(() => server.handle(viewer, 'joinstreamrequest', { streamIdentifier }), { code: 'not_in_channel' });
});

test('miniatura só é publicada em stream público', () => {
  const { server, login, notes } = setup();
  const host = login('host');
  const viewer = login('viewer');
  const { streamIdentifier } = server.handle(host, 'setupstream', { access: StreamAccess.PRIVATE });
  const thumb = 'data:image/jpeg;base64,AAAA';
  assert.deepEqual(server.handle(host, 'streamthumbnail', { streamIdentifier, thumbnail: thumb }), { published: false });
  server.handle(host, 'updatestream', { streamIdentifier, access: StreamAccess.PUBLIC });
  assert.deepEqual(server.handle(host, 'streamthumbnail', { streamIdentifier, thumbnail: thumb }), { published: true });
  assert.equal(notes(viewer, 'notifystreamthumbnail')[0].args.thumbnail, thumb);
});

test('modo SFU é recusado (servidor só anuncia STREAM_P2P)', () => {
  const { server, login } = setup();
  const host = login('host');
  assert.throws(() => server.handle(host, 'setupstream', { mode: 2 }), { code: 'mode_not_supported' });
  // O relay de mídia só existe no servidor nativo: aqui STREAM_SFU nunca é anunciado.
  const other = server.connect();
  const view = server.handle(other, 'login', { nickname: 'x', uid: 'uid-x' });
  assert.deepEqual(view.server.capabilityExtensions, ['STREAM_P2P']);
});

test('aceite informa peerClid (P2P: o próprio dono)', () => {
  const { server, login, notes } = setup();
  const host = login('host');
  const viewer = login('viewer');
  const { streamIdentifier } = server.handle(host, 'setupstream', {});
  server.handle(viewer, 'joinstreamrequest', { streamIdentifier });
  server.handle(host, 'respondjoinstreamrequest', { streamIdentifier, clid: viewer, accept: true });
  const [resp] = notes(viewer, 'notifyrespondjoinstreamrequest');
  assert.equal(resp.args.peerClid, host);
  assert.equal(resp.args.ownerClid, host);
});

test('bot do Discord: liveSnapshot só lista streams com announce, com quem e qual app', () => {
  const { server, login, notes } = setup();
  const host = login('host');
  const other = login('other');
  const { streamIdentifier: shown } = server.handle(host, 'setupstream', { name: 'Ranked', app: 'Counter-Strike 2', type: 3 });
  server.handle(other, 'setupstream', { name: 'Segredo', announce: false });

  assert.equal(notes(other, 'notifystreamstarted')[0].args.stream.announce, true);
  let live = server.liveSnapshot();
  assert.equal(live.streams.length, 1);
  assert.equal(live.streams[0].id, shown);
  assert.equal(live.streams[0].owner, 'host');
  assert.equal(live.streams[0].app, 'Counter-Strike 2');
  assert.equal(live.streams[0].channel, 'Lobby');
  assert.equal(live.streams[0].uid, undefined);
  assert.equal(live.streams[0].thumbnail, undefined);
  // Contagens contam também streams sem anúncio, mas sem dizer de quem.
  assert.deepEqual(live.stats, { clients: 2, streams: 2, viewers: 0, mbps: null });

  server.handle(host, 'updatestream', { streamIdentifier: shown, announce: false });
  assert.equal(server.liveSnapshot().streams.length, 0);
  server.handle(host, 'updatestream', { streamIdentifier: shown, announce: true, app: 'Chrome' });
  live = server.liveSnapshot();
  assert.equal(live.streams[0].app, 'Chrome');
});

test('webhook do bot: onLiveChanged dispara no ciclo de vida do stream e na troca de apelido', () => {
  let calls = 0;
  const server = new VirtualServer({ send: () => {}, onLiveChanged: () => { calls++; } });
  const host = server.connect();
  server.handle(host, 'login', { nickname: 'host', uid: 'uid-host' });
  assert.equal(calls, 0);
  const { streamIdentifier } = server.handle(host, 'setupstream', {});
  assert.equal(calls, 1);
  server.handle(host, 'updatestream', { streamIdentifier, app: 'Chrome' });
  assert.equal(calls, 2);
  server.handle(host, 'clientupdate', { nickname: 'novo' });
  assert.equal(calls, 3);
  server.disconnect(host);
  assert.equal(calls, 4);
});

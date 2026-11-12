import { test } from 'node:test';
import assert from 'node:assert/strict';
import { MAX_CONTENT, chooseLinkAction, linkSettings, rewriteXLinks, webhookName } from '../src/xlinks.js';

test('troca x.com e twitter.com (com www/mobile) por fxtwitter e tira o rastreio', () => {
  const cases = [
    ['https://x.com/jack/status/20', 'https://fxtwitter.com/jack/status/20'],
    ['https://twitter.com/jack/status/20?s=20&t=abc', 'https://fxtwitter.com/jack/status/20'],
    ['http://www.x.com/Some_User/status/123456789012345678?t=xyz#m', 'https://fxtwitter.com/Some_User/status/123456789012345678'],
    ['https://mobile.twitter.com/jack/statuses/20', 'https://fxtwitter.com/jack/status/20'],
    ['https://x.com/jack/status/20/photo/1', 'https://fxtwitter.com/jack/status/20/photo/1'],
    ['https://x.com/i/web/status/20', 'https://fxtwitter.com/i/web/status/20'],
    ['https://X.COM/jack/status/20/', 'https://fxtwitter.com/jack/status/20'],
  ];
  for (const [input, expected] of cases) assert.equal(rewriteXLinks(input).content, expected, input);
});

test('mantém o resto do texto e não repete links', () => {
  const { content, links } = rewriteXLinks('olha isso https://x.com/a/status/1?s=20 e (https://twitter.com/a/status/1) kkk');
  assert.equal(content, 'olha isso https://fxtwitter.com/a/status/1 e (https://fxtwitter.com/a/status/1) kkk');
  assert.deepEqual(links, ['https://fxtwitter.com/a/status/1']);
});

test('ignora links que não são post, outros domínios e links sem prévia (< >)', () => {
  for (const text of ['https://x.com/jack', 'https://notx.com/a/status/1', 'https://fxtwitter.com/a/status/1',
    '<https://x.com/a/status/1>', 'sem link nenhum', '']) {
    assert.deepEqual(rewriteXLinks(text).links, [], text);
  }
});

test('usa o serviço escolhido', () => {
  assert.equal(rewriteXLinks('https://x.com/a/status/1', 'fixupx.com').content, 'https://fixupx.com/a/status/1');
});

test('config por servidor: desligada e no modo responder por padrão', () => {
  assert.deepEqual(linkSettings(undefined), { enabled: false, mode: 'responder', service: 'fxtwitter.com', scope: 'usuarios', networks: { x: true, tiktok: true, instagram: true } });
  assert.deepEqual(linkSettings({ enabled: true, mode: 'substituir', service: 'vxtwitter.com', scope: 'usuarios' }),
    { enabled: true, mode: 'substituir', service: 'vxtwitter.com', scope: 'usuarios', networks: { x: true, tiktok: true, instagram: true } });
  assert.deepEqual(linkSettings({ enabled: 'sim', mode: 'xyz', service: 'evil.com', scope: 'x' }),
    { enabled: false, mode: 'responder', service: 'fxtwitter.com', scope: 'usuarios', networks: { x: true, tiktok: true, instagram: true } });
});

test('escolhe entre webhook e resposta', () => {
  const ok = { mode: 'substituir', isReply: false, hasExtras: false, contentLength: 50, canWebhook: true, canDelete: true, canSend: true };
  assert.equal(chooseLinkAction(ok), 'webhook');
  assert.equal(chooseLinkAction({ ...ok, mode: 'responder' }), 'reply');
  assert.equal(chooseLinkAction({ ...ok, isReply: true }), 'reply');
  assert.equal(chooseLinkAction({ ...ok, hasExtras: true }), 'reply');
  assert.equal(chooseLinkAction({ ...ok, contentLength: MAX_CONTENT + 1 }), 'reply');
  assert.equal(chooseLinkAction({ ...ok, canWebhook: false }), 'reply');
  assert.equal(chooseLinkAction({ ...ok, canDelete: false }), 'reply');
  assert.equal(chooseLinkAction({ ...ok, mode: 'responder', canSend: false }), null);
});

test('nome do webhook aceito pelo Discord', () => {
  assert.equal(webhookName('  '), 'Alguém');
  assert.equal(webhookName('x'.repeat(100)).length, 80);
  assert.doesNotMatch(webhookName('Discord Fan'), /discord/i);
  assert.doesNotMatch(webhookName('clyde'), /clyde/i);
});

test('TikTok vira tnktok.com (vídeo, foto e link curto) e Instagram vira uuinstagram.com (reel, post)', async () => {
  const { rewriteLinks, linkSettings } = await import('../src/xlinks.js');
  const s = linkSettings({});
  const cases = [
    ['https://www.tiktok.com/@fulano/video/7312345678901234567?is_from_webapp=1&sender_device=pc', 'https://tnktok.com/@fulano/video/7312345678901234567'],
    ['https://vm.tiktok.com/ZMabc123/', 'https://vm.tnktok.com/ZMabc123/'],
    ['https://vt.tiktok.com/ZSxyz/', 'https://vt.tnktok.com/ZSxyz/'],
    ['https://www.tiktok.com/t/ZT8abc/', 'https://tnktok.com/t/ZT8abc/'],
    ['(https://www.tiktok.com/@a.b/photo/123).', '(https://tnktok.com/@a.b/photo/123).'],
    ['https://www.instagram.com/reel/DQabc-12_x/?igsh=MWQ1ZGUxMzBkMA==', 'https://uuinstagram.com/reel/DQabc-12_x/'],
    ['https://instagram.com/p/Cxyz123/', 'https://uuinstagram.com/p/Cxyz123/'],
    ['https://www.instagram.com/fulano/reel/DQq1w2e3r/', 'https://uuinstagram.com/reel/DQq1w2e3r/'],
    ['https://www.instagram.com/reels/DQzzz/', 'https://uuinstagram.com/reels/DQzzz/'],
  ];
  for (const [input, expected] of cases) assert.equal(rewriteLinks(input, s).content, expected, input);
  // Perfil, página inicial e link entre < > ficam como estão.
  for (const text of ['https://www.tiktok.com/@fulano', 'https://www.tiktok.com/explore', 'https://instagram.com/fulano/',
    '<https://www.tiktok.com/@a/video/1>']) assert.deepEqual(rewriteLinks(text, s).links, [], text);
  // Misturado, e com uma rede desligada no servidor.
  const mixed = 'https://x.com/a/status/1?s=20 https://tiktok.com/@a/video/2 https://instagram.com/reel/R1/';
  assert.equal(rewriteLinks(mixed, s).links.length, 3);
  assert.deepEqual(rewriteLinks(mixed, linkSettings({ networks: { tiktok: false, instagram: false } })).links, ['https://fxtwitter.com/a/status/1']);
});

test('posts do X sem vídeo ficam com o link original', async () => {
  const { rewriteLinks, xStatusIds } = await import('../src/xlinks.js');
  const text = 'foto https://x.com/a/status/1?s=20 vídeo https://x.com/b/status/2 <https://x.com/c/status/3>';
  assert.deepEqual(xStatusIds(text), ['1', '2']);
  const { content, links } = rewriteLinks(text, linkSettings({}), { keepX: new Set(['1']) });
  assert.equal(content, 'foto https://x.com/a/status/1?s=20 vídeo https://fxtwitter.com/b/status/2 <https://x.com/c/status/3>');
  assert.deepEqual(links, ['https://fxtwitter.com/b/status/2']);
  assert.deepEqual(rewriteLinks('https://x.com/a/status/1', linkSettings({}), { keepX: new Set(['1']) }).links, []);
});

test('tweetHasVideo: vídeo ou GIF no post ou no citado', async () => {
  const { tweetHasVideo } = await import('../src/xlinks.js');
  assert.equal(tweetHasVideo({ media: { videos: [{ type: 'video' }] } }), true);
  assert.equal(tweetHasVideo({ media: { all: [{ type: 'gif' }] } }), true);
  assert.equal(tweetHasVideo({ media: { photos: [{ type: 'photo' }], all: [{ type: 'photo' }] } }), false);
  assert.equal(tweetHasVideo({ text: 'só texto' }), false);
  assert.equal(tweetHasVideo({ media: { photos: [{}] }, quote: { media: { videos: [{}] } } }), true);
});

test('XMediaChecker consulta o fxtwitter, guarda em cache e devolve null quando falha', async () => {
  const { XMediaChecker } = await import('../src/xlinks.js');
  const calls = [];
  const answers = {
    1: { ok: true, json: async () => ({ code: 200, tweet: { media: { videos: [{ type: 'video' }] } } }) },
    2: { ok: true, json: async () => ({ code: 200, tweet: { media: { photos: [{}] } } }) },
    3: { ok: false, json: async () => ({ code: 404 }) },
  };
  const checker = new XMediaChecker({
    fetch: async url => {
      calls.push(url);
      const id = url.split('/').pop();
      if (id === '4') throw new Error('rede');
      return answers[id];
    },
  });
  assert.equal(await checker.hasVideo('1'), true);
  assert.equal(await checker.hasVideo('2'), false);
  assert.equal(await checker.hasVideo('3'), null);
  assert.equal(await checker.hasVideo('4'), null);
  assert.equal(await checker.hasVideo('1'), true);
  assert.equal(calls.filter(u => u.endsWith('/1')).length, 1, 'cache');
  assert.equal(calls[0], 'https://api.fxtwitter.com/status/1');
  await checker.hasVideo('3');
  assert.equal(calls.filter(u => u.endsWith('/3')).length, 2, 'falha não fica em cache');
});

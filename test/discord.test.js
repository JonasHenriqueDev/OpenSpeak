import test from 'node:test';
import assert from 'node:assert/strict';

import { discordContext, discordNickname } from '../public/js/activity/discord.js';

test('apelido usa o nome de exibição do Discord com o sufixo (discord)', () => {
  assert.equal(discordNickname({ username: 'jonas123', global_name: 'Jonas' }), 'Jonas (discord)');
  assert.equal(discordNickname({ username: 'jonas123', global_name: null }), 'jonas123 (discord)');
});

test('apelido longo é cortado para caber nos 32 caracteres', () => {
  const nick = discordNickname({ global_name: 'Um nome de exibição bem comprido demais' });
  assert.ok(nick.length <= 32);
  assert.ok(nick.endsWith(' (discord)'));
});

test('contexto do Discord vem da URL da atividade', () => {
  const loc = { hostname: '1234567890.discordsays.com', search: '?frame_id=f&instance_id=i&platform=desktop' };
  assert.deepEqual(discordContext(loc), { frameId: 'f', clientId: '1234567890', platform: 'desktop', channelId: null });
  assert.equal(discordContext({ hostname: 'openspeak.duckdns.org', search: '' }), null);
});

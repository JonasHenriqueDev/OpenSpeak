import test from 'node:test';
import assert from 'node:assert/strict';
import { isTailnetAddress, isTailnetServer, tailnetCandidate, tailnetSdp, privateEntryUrl, advertiseTailnetCandidate, advertiseTailnetSdp } from '../public/js/core/tailnet.js';

test('private deployment accepts only authenticated Tailscale HTTPS endpoints', () => {
  assert.ok(isTailnetServer('wss://desktop.example.ts.net/ws'));
  for (const url of ['ws://desktop.example.ts.net/ws', 'wss://example.com/ws', 'wss://a.ts.net.example.com/ws', 'wss://user:secret@a.ts.net/ws', 'invalid'])
    assert.equal(isTailnetServer(url), false);
});

test('browser mDNS advertisements use the supplied tailnet IP without exposing LAN or relay routes', () => {
  const candidate = 'candidate:1 1 udp 123 browser.local 4000 typ host';
  assert.equal(advertiseTailnetCandidate(candidate, '100.75.12.74'), candidate.replace('browser.local', '100.75.12.74'));
  assert.equal(advertiseTailnetCandidate(candidate.replace('browser.local', '192.168.0.1'), '100.75.12.74'), null);
  assert.equal(advertiseTailnetCandidate(candidate.replace('host', 'relay'), '100.75.12.74'), null);
  assert.throws(() => advertiseTailnetCandidate(candidate, '8.8.8.8'));
  assert.equal(advertiseTailnetSdp(`v=0\r\na=${candidate}\r\n`, '100.75.12.74'), `v=0\r\na=${candidate.replace('browser.local', '100.75.12.74')}\r\n`);
});

test('public entry keeps nickname and profile when opening the private HTTPS client', () => {
  const url = new URL(privateEntryUrl('wss://desktop.example.ts.net/ws', ' Jonas & Teste ', 'a'));
  assert.equal(url.origin, 'https://desktop.example.ts.net');
  assert.equal(url.searchParams.get('nickname'), 'Jonas & Teste');
  assert.equal(url.searchParams.get('profile'), 'a');
  assert.equal(url.searchParams.get('autoconnect'), '1');
  assert.throws(() => privateEntryUrl('wss://example.com/ws', 'Jonas'));
});

test('tailnet candidates exclude public, LAN and TURN routes', () => {
  for (const ip of ['100.64.0.1', '100.127.255.254', 'fd7a:115c:a1e0::1234']) assert.ok(isTailnetAddress(ip));
  for (const ip of ['100.63.1.1', '100.128.1.1', '100.999.1.1', '192.168.0.1', '8.8.8.8']) assert.equal(isTailnetAddress(ip), false);
  const cand = (ip, type = 'host') => `candidate:1 1 udp 123 ${ip} 4000 typ ${type}`;
  assert.ok(tailnetCandidate(cand('100.75.12.74')));
  assert.ok(tailnetCandidate(cand('abc-123.local')));
  assert.equal(tailnetCandidate(cand('100.75.12.74', 'relay')), false);
  assert.equal(tailnetCandidate(cand('192.168.0.1')), false);
  const sdp = `v=0\r\na=${cand('192.168.0.1')}\r\na=${cand('100.75.12.74')}\r\n`;
  assert.equal(tailnetSdp(sdp), `v=0\r\na=${cand('100.75.12.74')}\r\n`);
});

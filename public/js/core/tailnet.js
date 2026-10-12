export function isTailnetAddress(address) {
  const parts = address.split('.');
  if (parts.length === 4 && parts.every(p => /^\d{1,3}$/.test(p) && Number(p) <= 255))
    return Number(parts[0]) === 100 && Number(parts[1]) >= 64 && Number(parts[1]) <= 127;
  return /^fd7a:115c:a1e0:/i.test(address);
}

export function isTailnetServer(url) {
  try {
    const parsed = new URL(url);
    return parsed.protocol === 'wss:' && parsed.hostname.endsWith('.ts.net') &&
      !parsed.username && !parsed.password;
  } catch { return false; }
}

export function tailnetCandidate(candidate) {
  if (!candidate) return true; // end-of-candidates
  const fields = candidate.replace(/^a=/, '').trim().split(/\s+/);
  const type = fields[fields.indexOf('typ') + 1];
  // Browser host candidates can be masked with mDNS. Native endpoints advertise
  // numeric Tailscale candidates, so connectivity checks still reach the tailnet.
  return type === 'host' && (isTailnetAddress(fields[4] || '') || /^[\w-]+\.local$/i.test(fields[4] || ''));
}

export function tailnetSdp(sdp) {
  return sdp.split(/\r?\n/).filter(line => !line.startsWith('a=candidate:') || tailnetCandidate(line)).join('\r\n');
}

export function privateEntryUrl(serverUrl, nickname, profile = '') {
  if (!isTailnetServer(serverUrl)) throw new Error('Servidor Tailscale inválido');
  const server = new URL(serverUrl);
  const page = new URL(`https://${server.host}/`);
  page.searchParams.set('nickname', nickname.trim().slice(0, 32));
  page.searchParams.set('autoconnect', '1');
  if (profile) page.searchParams.set('profile', profile.replace(/[^\w-]/g, ''));
  return page.href;
}

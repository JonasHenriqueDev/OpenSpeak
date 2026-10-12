import { isTailnetAddress } from './tailnet.js';

// Passive ICE gathering only: no media capture or permission prompts.
// Browsers may return masked mDNS names instead of network addresses.
export async function detectTailnetAddress({ PeerConnection = globalThis.RTCPeerConnection, timeoutMs = 2500 } = {}) {
  if (!PeerConnection) return null;
  let pc;
  try { pc = new PeerConnection({ iceServers: [] }); } catch { return null; }
  return new Promise(resolve => {
    let done = false, address = null;
    const finish = () => {
      if (done) return;
      done = true; clearTimeout(timer);
      try { pc.close(); } catch { /* already closed */ }
      resolve(address);
    };
    const timer = setTimeout(finish, timeoutMs);
    pc.addEventListener('icecandidate', event => {
      if (done) return;
      if (!event.candidate) { finish(); return; }
      const ip = event.candidate.address || event.candidate.candidate?.split(/\s+/)[4];
      if (ip && isTailnetAddress(ip)) {
        address = ip;
        if (ip.includes('.')) finish();
      }
    });
    try {
      pc.createDataChannel('tailnet-probe');
      Promise.resolve(pc.createOffer()).then(offer => {
        if (!done) return pc.setLocalDescription(offer);
      }).catch(finish);
    } catch { finish(); }
  });
}

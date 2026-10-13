import { StreamMode, RELAY_CLID } from '../core/protocol.js';

// ICE candidate types describe the route to a peer, not whether that peer is an SFU.
export function statsTransmissionMode({ own, stream, view } = {}) {
  if (view?.peerClid === RELAY_CLID) return StreamMode.SFU;
  const mode = own?.config?.mode ?? stream?.mode;
  return mode === StreamMode.SFU || mode === StreamMode.P2P ? mode : null;
}

#pragma once
// Regras do simulcast (SFU) usadas pelos dois lados: o transmissor (cliente nativo)
// e o relay do servidor precisam concordar sobre o bitrate da camada baixa, porque
// enquanto ela está pausada esse bitrate vai para a camada alta.

#include <algorithm>

namespace openspeak::simulcast {

/// Vale a pena mandar a camada baixa? Abaixo disso a alta já é leve.
inline bool worthIt(int bitrate, int height) { return bitrate >= 1'200'000 && (height <= 0 || height >= 540); }

/// Bitrate da camada baixa para um stream de `streamBitrate` (a alta).
inline int lowLayerBitrate(int streamBitrate) { return std::clamp(streamBitrate / 5, 300'000, 900'000); }

/// Bitrate máximo da camada alta: o do stream, mais o da baixa enquanto ela está
/// pausada (o upload do transmissor continua o mesmo).
inline int highLayerBitrate(int streamBitrate, bool lowPaused)
{
    return streamBitrate + (lowPaused ? lowLayerBitrate(streamBitrate) : 0);
}

} // namespace openspeak::simulcast

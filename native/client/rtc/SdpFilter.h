#pragma once
// Ajustes de SDP para interoperar com o navegador.
//
// Quando o transmissor é um navegador, a oferta traz vários codecs e
// mecanismos que o lado nativo (libdatachannel) não implementa: RTX, RED,
// ULPFEC/FlexFEC e transport-cc. Removemos tudo isso antes de responder, para
// que o navegador envie H.264 "puro" (com NACK/PLI/REMB) e Opus.

#include <QSet>
#include <QString>

namespace openspeak::sdp {

/// Mantém só os codecs permitidos (nomes do rtpmap, ex.: "H264", "opus").
QString filterCodecs(const QString &sdp, const QSet<QString> &allowedVideo, const QSet<QString> &allowedAudio);

/// Garante Opus estéreo e bitrate máximo no fmtp (áudio do sistema).
QString opusStereo(const QString &sdp, int maxAverageBitrate);

} // namespace openspeak::sdp

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

/// Modo SFU: entre as variantes de H.264 da oferta, fica só com
/// packetization-mode=1 + Constrained Baseline (o que o relay anuncia aos
/// espectadores). Sem nenhuma compatível, devolve o SDP como veio.
QString keepH264ConstrainedBaseline(const QString &sdp);

/// Troca o IP dos candidatos host IPv4 por `ip` (NAT 1:1). Aceita um candidato
/// avulso ("candidate:..."/"a=candidate:...") ou um SDP inteiro.
QString advertiseHostIp(const QString &text, const QString &ip);

/// Identifica um candidato por transporte, endereço, porta e tipo (ignora
/// fundação e prioridade), para descartar repetidos.
QString candidateKey(const QString &candidate);

} // namespace openspeak::sdp

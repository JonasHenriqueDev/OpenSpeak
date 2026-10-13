#include "SfuForwarder.h"

#include <algorithm>

namespace openspeak::sfu {

namespace {

uint8_t u8(const std::byte *p) { return std::to_integer<uint8_t>(*p); }
uint16_t be16(const std::byte *p) { return uint16_t(u8(p) << 8 | u8(p + 1)); }
uint32_t be32(const std::byte *p)
{
    return uint32_t(u8(p)) << 24 | uint32_t(u8(p + 1)) << 16 | uint32_t(u8(p + 2)) << 8 | uint32_t(u8(p + 3));
}
void put16(std::byte *p, uint16_t v)
{
    p[0] = std::byte(v >> 8);
    p[1] = std::byte(v & 0xFF);
}
void put32(std::byte *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = std::byte((v >> (24 - 8 * i)) & 0xFF);
}

uint32_t ticks(std::chrono::steady_clock::duration d, uint32_t clockRate)
{
    return uint32_t(std::max(0.0, std::chrono::duration<double>(d).count()) * clockRate);
}

} // namespace

// ================================================================ EgressSink

void EgressSink::attach(std::shared_ptr<rtc::Track> video, std::shared_ptr<rtc::Track> audio, uint32_t videoSsrc, uint32_t audioSsrc)
{
    std::lock_guard lock(m_mutex);
    m_video = std::move(video);
    m_audio = std::move(audio);
    m_videoSsrc = videoSsrc;
    m_audioSsrc = audioSsrc;
}

void EgressSink::detach()
{
    std::lock_guard lock(m_mutex);
    m_video.reset();
    m_audio.reset();
}

void EgressSink::send(Kind kind, const rtc::binary &packet)
{
    std::shared_ptr<rtc::Track> track;
    uint32_t ssrc = 0;
    {
        std::lock_guard lock(m_mutex);
        track = kind == Kind::Video ? m_video : m_audio;
        ssrc = kind == Kind::Video ? m_videoSsrc : m_audioSsrc;
    }
    if (!track || !track->isOpen() || packet.size() < 12)
        return;
    rtc::binary copy = packet;
    put32(copy.data() + 8, ssrc);
    try {
        track->send(std::move(copy));
    } catch (const std::exception &) {
        // track fechando durante uma reconexão do espectador
    }
}

// ================================================================= Forwarder

void Forwarder::setIngestVideoTrack(const std::shared_ptr<rtc::Track> &track)
{
    std::lock_guard lock(m_mutex);
    m_ingestVideo = track;
}

void Forwarder::onRtp(Kind kind, const std::byte *data, size_t size)
{
    // Cabeçalho RTP: V=2, CC CSRCs e, se X, uma extensão (perfil + tamanho em palavras).
    if (size < 12 || (u8(data) >> 6) != 2)
        return;
    const uint8_t b0 = u8(data);
    const size_t header = 12 + 4 * size_t(b0 & 0x0F);
    if (size < header)
        return;
    size_t payloadAt = header;
    if (b0 & 0x10) {
        if (size < header + 4)
            return;
        payloadAt = header + 4 + 4 * size_t(be16(data + header + 2));
        if (size < payloadAt)
            return;
    }
    const uint16_t seq = be16(data + 2);
    const uint32_t ts = be32(data + 4);
    const uint32_t ssrc = be32(data + 8);
    const auto now = Clock::now();

    uint16_t outSeq = 0;
    uint32_t outTs = 0;
    std::vector<std::shared_ptr<EgressSink>> sinks;
    std::shared_ptr<rtc::Track> pli;
    {
        std::lock_guard lock(m_mutex);
        Lane &l = m_lanes[int(kind)];
        if (!l.started || ssrc != l.srcSsrc) {
            // Fonte nova (início ou transmissor reconectado): continua a
            // numeração de saída de onde parou, avançando o relógio RTP pelo
            // tempo real que passou.
            if (l.started) {
                const uint32_t step = std::max<uint32_t>(1, ticks(now - l.lastAt, clockRate(kind)));
                l.seqOffset = uint16_t(l.lastSeq + 1 - seq);
                l.tsOffset = l.lastTs + step - ts;
            }
            l.started = true;
            l.srcSsrc = ssrc;
            l.haveSr = false;
            l.lastSeq = uint16_t(seq + l.seqOffset - 1);
            l.lastTs = ts + l.tsOffset;
            l.lastAt = now;
            if (kind == Kind::Video)
                m_keyFramePending = true; // os espectadores precisam de um IDR da fonte nova
        }
        outSeq = uint16_t(seq + l.seqOffset);
        outTs = ts + l.tsOffset;
        if (int16_t(outSeq - l.lastSeq) > 0) { // ignora retransmissões/fora de ordem
            l.lastSeq = outSeq;
            l.lastTs = outTs;
            l.lastAt = now;
        }
        if (kind == Kind::Video)
            pli = takeKeyFrameRequestLocked(now);
        sinks = m_sinks;
    }
    if (pli) {
        try {
            pli->requestKeyframe();
        } catch (const std::exception &) {
        }
    }
    if (sinks.empty())
        return;

    rtc::binary out;
    out.reserve(header + (size - payloadAt));
    out.assign(data, data + header);
    out[0] = std::byte(b0 & ~0x10); // sem extensão
    out[1] = std::byte((u8(data + 1) & 0x80) | (kind == Kind::Video ? VideoPayloadType : AudioPayloadType));
    put16(out.data() + 2, outSeq);
    put32(out.data() + 4, outTs);
    out.insert(out.end(), data + payloadAt, data + size);
    for (const auto &sink : sinks)
        sink->send(kind, out);
}

void Forwarder::onSenderReport(Kind kind, uint32_t ssrc, uint32_t rtpTimestamp)
{
    std::lock_guard lock(m_mutex);
    Lane &l = m_lanes[int(kind)];
    if (!l.started || ssrc != l.srcSsrc)
        return;
    l.haveSr = true;
    l.srRtp = rtpTimestamp;
    l.srAt = Clock::now();
}

std::optional<uint32_t> Forwarder::rtpNow(Kind kind) const
{
    std::lock_guard lock(m_mutex);
    const Lane &l = m_lanes[int(kind)];
    if (!l.started)
        return std::nullopt;
    const auto now = Clock::now();
    // Com SR do transmissor, seguimos a âncora dele (mesma referência para
    // áudio e vídeo); sem SR, o último pacote repassado vale como "agora".
    if (l.haveSr)
        return l.srRtp + l.tsOffset + ticks(now - l.srAt, clockRate(kind));
    return l.lastTs + ticks(now - l.lastAt, clockRate(kind));
}

void Forwarder::addSink(const std::shared_ptr<EgressSink> &sink)
{
    std::lock_guard lock(m_mutex);
    if (std::find(m_sinks.begin(), m_sinks.end(), sink) == m_sinks.end())
        m_sinks.push_back(sink);
}

void Forwarder::removeSink(const std::shared_ptr<EgressSink> &sink)
{
    std::lock_guard lock(m_mutex);
    m_sinks.erase(std::remove(m_sinks.begin(), m_sinks.end(), sink), m_sinks.end());
}

std::shared_ptr<rtc::Track> Forwarder::takeKeyFrameRequestLocked(Clock::time_point now)
{
    if (!m_keyFramePending || now - m_lastKeyFrameRequest < KeyFrameInterval)
        return {};
    auto track = m_ingestVideo.lock();
    if (!track || !track->isOpen())
        return {};
    m_keyFramePending = false;
    m_lastKeyFrameRequest = now;
    return track;
}

void Forwarder::requestKeyFrame()
{
    std::shared_ptr<rtc::Track> track;
    {
        std::lock_guard lock(m_mutex);
        m_keyFramePending = true;
        track = takeKeyFrameRequestLocked(Clock::now());
    }
    if (!track)
        return; // sai com o próximo pacote de vídeo, quando a janela abrir
    try {
        track->requestKeyframe();
    } catch (const std::exception &) {
    }
}

void Forwarder::requestBitrate(unsigned int bitrate)
{
    std::shared_ptr<rtc::Track> track;
    {
        std::lock_guard lock(m_mutex);
        track = m_ingestVideo.lock();
    }
    if (!track || !track->isOpen())
        return;
    try {
        track->requestBitrate(bitrate);
    } catch (const std::exception &) {
    }
}

} // namespace openspeak::sfu

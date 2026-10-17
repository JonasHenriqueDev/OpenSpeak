#include "RtpRecovery.h"

#include <algorithm>

namespace openspeak {

namespace {

using namespace std::chrono_literals;

uint8_t u8(const std::byte *p) { return std::to_integer<uint8_t>(*p); }
uint16_t be16(const std::byte *p) { return uint16_t(u8(p) << 8 | u8(p + 1)); }
uint32_t be32(const std::byte *p)
{
    return uint32_t(u8(p)) << 24 | uint32_t(u8(p + 1)) << 16 | uint32_t(u8(p + 2)) << 8 | uint32_t(u8(p + 3));
}
void put16(rtc::binary &b, uint16_t v)
{
    b.push_back(std::byte(v >> 8));
    b.push_back(std::byte(v & 0xFF));
}
void put32(rtc::binary &b, uint32_t v)
{
    put16(b, uint16_t(v >> 16));
    put16(b, uint16_t(v & 0xFFFF));
}

/// Procura SPS (7) e início de IDR (5) no payload H.264 (RFC 6184).
void scanH264(const std::byte *data, size_t size, size_t off, bool &sps, bool &idr)
{
    auto check = [&](uint8_t type) {
        sps |= type == 7;
        idr |= type == 5;
    };
    if (off >= size)
        return;
    const uint8_t type = u8(data + off) & 0x1F;
    if (type == 24) { // STAP-A
        size_t i = off + 1;
        while (i + 2 < size) {
            const size_t n = be16(data + i);
            i += 2;
            check(u8(data + i) & 0x1F);
            i += n;
        }
    } else if (type == 28) { // FU-A: só o fragmento inicial diz o tipo
        if (off + 1 < size && (u8(data + off + 1) & 0x80))
            check(u8(data + off + 1) & 0x1F);
    } else {
        check(type);
    }
}

} // namespace

rtc::binary buildGenericNack(uint32_t senderSsrc, uint32_t mediaSsrc, std::vector<uint16_t> seqs)
{
    // FCI = PID + BLP (bit i = PID + i + 1 também perdido).
    std::vector<std::pair<uint16_t, uint16_t>> fci;
    for (const uint16_t s : seqs) {
        if (!fci.empty()) {
            const uint16_t d = uint16_t(s - fci.back().first);
            if (d == 0)
                continue;
            if (d <= 16) {
                fci.back().second |= uint16_t(1u << (d - 1));
                continue;
            }
        }
        fci.emplace_back(s, 0);
    }
    rtc::binary b;
    b.reserve(12 + 4 * fci.size());
    b.push_back(std::byte(0x80 | 1)); // V=2, FMT=1
    b.push_back(std::byte(205));
    put16(b, uint16_t(2 + fci.size()));
    put32(b, senderSsrc);
    put32(b, mediaSsrc);
    for (const auto &[pid, blp] : fci) {
        put16(b, pid);
        put16(b, blp);
    }
    return b;
}

rtc::binary buildPli(uint32_t senderSsrc, uint32_t mediaSsrc)
{
    rtc::binary b;
    b.push_back(std::byte(0x80 | 1)); // V=2, FMT=1
    b.push_back(std::byte(206));
    put16(b, 2);
    put32(b, senderSsrc);
    put32(b, mediaSsrc);
    return b;
}

// ---------------------------------------------------------------- buffer

RtpRecoveryBuffer::Clock::duration RtpRecoveryBuffer::retryInterval() const
{
    return std::clamp<Clock::duration>(m_srtt * 5 / 4, 15ms, 150ms);
}

RtpRecoveryBuffer::Clock::duration RtpRecoveryBuffer::giveUpAfter() const
{
    // ~3 tentativas; o pacote pode vir de duas pernas (transmissor -> SFU -> espectador).
    return std::clamp<Clock::duration>(m_srtt * 3 + 40ms, 120ms, 500ms);
}

int64_t RtpRecoveryBuffer::unwrap(uint16_t seq) const
{
    return m_highest + int16_t(uint16_t(seq - uint16_t(m_highest)));
}

void RtpRecoveryBuffer::requestKeyFrame(Clock::time_point now, Events &ev, bool force)
{
    const auto interval = force ? Clock::duration(200ms) : Clock::duration(KeyFrameRetry);
    if (now - m_lastKeyRequest < interval)
        return;
    m_lastKeyRequest = now;
    ev.keyFrame = true;
    m_stats.keyFrameRequests++;
}

void RtpRecoveryBuffer::reset(int64_t ext, Clock::time_point now, Events &ev)
{
    m_packets.clear();
    m_missing.clear();
    m_highest = ext;
    m_next = ext;
    m_aligned = false;
    m_waitKey = true;
    if (m_mode == Mode::Frames)
        requestKeyFrame(now, ev, true);
}

void RtpRecoveryBuffer::push(rtc::message_ptr packet, Clock::time_point now, rtc::message_vector &out, Events &ev)
{
    const std::byte *data = packet->data();
    const size_t size = packet->size();
    if (size < 12 || (u8(data) >> 6) != 2) {
        out.push_back(std::move(packet));
        return;
    }
    size_t payload = 12 + 4 * size_t(u8(data) & 0x0F);
    if ((u8(data) & 0x10) && payload + 4 <= size)
        payload += 4 + 4 * size_t(be16(data + payload + 2));
    const uint16_t seq = be16(data + 2);
    const uint32_t ssrc = be32(data + 8);

    if (!m_started || ssrc != m_ssrc) {
        m_started = true;
        m_ssrc = ssrc;
        reset(int64_t(1) << 20 | seq, now, ev);
    }
    ev.mediaSsrc = m_ssrc;

    int64_t ext = unwrap(seq);
    if (ext - m_highest > MaxGap) { // salto grande: o transmissor recomeçou
        reset(int64_t(1) << 20 | seq, now, ev);
        ext = m_highest;
    }

    bool fresh = false;
    if (ext > m_highest) {
        for (int64_t s = m_highest + 1; s < ext; ++s) {
            m_missing[s] = {now, now, 1};
            ev.nack.push_back(uint16_t(s));
            m_stats.nackedPackets++;
        }
        m_highest = ext;
        fresh = true;
    } else if (auto it = m_missing.find(ext); it != m_missing.end()) {
        if (it->second.nacks == 1) { // amostra sem ambiguidade de RTT
            const auto sample = std::clamp<Clock::duration>(now - it->second.lastNack, 5ms, 1000ms);
            m_srtt = (m_srtt * 7 + sample) / 8;
        }
        m_missing.erase(it);
        m_stats.recoveredPackets++;
        fresh = true;
    } else if (ext == m_highest && m_packets.empty() && m_next == ext) {
        fresh = true; // primeiro pacote depois de reset
    }

    if (m_mode == Mode::PassThrough) {
        if (fresh)
            out.push_back(std::move(packet));
        return;
    }

    if (!fresh || ext < m_next || m_packets.count(ext))
        return; // duplicado ou tarde demais

    Packet p;
    p.timestamp = be32(data + 4);
    p.marker = u8(data + 1) & 0x80;
    scanH264(data, size, payload, p.hasSps, p.hasIdr);
    p.message = std::move(packet);
    m_packets.emplace(ext, std::move(p));
    emitFrames(out);
}

void RtpRecoveryBuffer::emitFrames(rtc::message_vector &out)
{
    while (!m_packets.empty() && m_packets.begin()->first == m_next) {
        // Quadro = sequência contínua a partir de m_next até o marker (ou até
        // o timestamp mudar). Se falta algum pacote, espera.
        const auto first = m_packets.begin();
        const uint32_t ts = first->second.timestamp;
        auto it = first;
        int64_t s = m_next;
        bool complete = false;
        auto end = m_packets.end();
        while (it != m_packets.end() && it->first == s) {
            if (it->second.timestamp != ts) {
                end = it;
                complete = true;
                break;
            }
            ++s;
            if ((it++)->second.marker) {
                end = it;
                complete = true;
                break;
            }
        }
        if (!complete)
            return;

        bool sps = false, idr = false;
        for (auto f = first; f != end; ++f) {
            sps |= f->second.hasSps;
            idr |= f->second.hasIdr;
        }
        bool deliver = true;
        if (!m_aligned)
            deliver = first->second.hasSps; // sem saber o início, só confia num quadro que abre com SPS
        else if (m_waitKey)
            deliver = sps || idr;
        if (deliver && (sps || idr))
            m_waitKey = false;

        for (auto f = first; f != end; ++f)
            if (deliver)
                out.push_back(std::move(f->second.message));
        if (!deliver)
            m_stats.droppedFrames++;
        m_packets.erase(first, end);
        m_next = s;
        m_aligned = true;
    }
}

void RtpRecoveryBuffer::giveUp(Clock::time_point now, Events &ev)
{
    // O quadro com o buraco já era: descarta o que veio dele e pula até o
    // próximo pacote recebido, sem saber onde começa o quadro seguinte.
    const int64_t hole = m_missing.empty() ? m_next : std::max(m_next, m_missing.begin()->first);
    if (m_packets.begin() != m_packets.end() && m_packets.begin()->first < hole)
        m_stats.droppedFrames++;
    m_packets.erase(m_packets.begin(), m_packets.upper_bound(hole));
    m_next = m_packets.empty() ? m_highest + 1 : m_packets.begin()->first;
    const auto stale = m_missing.lower_bound(m_next);
    m_stats.lostPackets += uint64_t(std::distance(m_missing.begin(), stale));
    m_missing.erase(m_missing.begin(), stale);
    m_aligned = false;
    const bool entering = !m_waitKey;
    m_waitKey = true;
    requestKeyFrame(now, ev, entering);
}

void RtpRecoveryBuffer::poll(Clock::time_point now, rtc::message_vector &out, Events &ev)
{
    if (!m_started)
        return;
    ev.mediaSsrc = m_ssrc;
    const auto limit = giveUpAfter();
    const auto retry = retryInterval();

    if (m_mode == Mode::PassThrough) {
        for (auto it = m_missing.begin(); it != m_missing.end();) {
            if (now - it->second.detected >= limit) {
                m_stats.lostPackets++;
                it = m_missing.erase(it);
                continue;
            }
            if (now - it->second.lastNack >= retry && it->second.nacks < MaxNacksPerPacket) {
                it->second.lastNack = now;
                it->second.nacks++;
                ev.nack.push_back(uint16_t(it->first));
            }
            ++it;
        }
        return;
    }

    // Prazo vencido no primeiro buraco (é ele que trava a entrega).
    while (!m_missing.empty() || m_packets.size() > MaxBuffered) {
        if (m_packets.size() <= MaxBuffered && now - m_missing.begin()->second.detected < limit)
            break;
        giveUp(now, ev);
        emitFrames(out);
    }
    for (auto &[seq, m] : m_missing) {
        if (now - m.lastNack >= retry && m.nacks < MaxNacksPerPacket) {
            m.lastNack = now;
            m.nacks++;
            ev.nack.push_back(uint16_t(seq));
        }
    }
    if (m_waitKey)
        requestKeyFrame(now, ev, false);
}

// ---------------------------------------------------------------- handler

void RtpRecoveryHandler::incoming(rtc::message_vector &messages, const rtc::message_callback &send)
{
    RtpRecoveryBuffer::Events ev;
    {
        std::lock_guard lock(m_mutex);
        const auto now = RtpRecoveryBuffer::Clock::now();
        rtc::message_vector out;
        out.reserve(messages.size());
        for (auto &m : messages) {
            if (!m)
                continue;
            if (m->type == rtc::Message::Binary)
                m_buffer.push(std::move(m), now, out, ev);
            else
                out.push_back(std::move(m));
        }
        m_buffer.poll(now, out, ev);
        messages.swap(out);
    }
    // Remetente sem SSRC próprio de mídia (só recebe): qualquer valor fixo serve.
    constexpr uint32_t ReceiverSsrc = 1;
    if (!ev.nack.empty())
        send(rtc::make_message(buildGenericNack(ReceiverSsrc, ev.mediaSsrc, std::move(ev.nack)), rtc::Message::Control));
    if (ev.keyFrame)
        send(rtc::make_message(buildPli(ReceiverSsrc, ev.mediaSsrc), rtc::Message::Control));
}

RtpRecoveryStats RtpRecoveryHandler::stats() const
{
    std::lock_guard lock(m_mutex);
    return m_buffer.stats();
}

} // namespace openspeak

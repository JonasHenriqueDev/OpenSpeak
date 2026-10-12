#pragma once
// MediaHandler do libdatachannel que mede o tráfego RTP/RTCP de uma track.
//  - Lado de envio: conta pacotes/bytes, lê Receiver Reports (perda, jitter,
//    RTT via LSR/DLSR), REMB e NACKs (retransmissões).
//  - Lado de recebimento: conta pacotes/bytes, perda por sequência e jitter
//    entre chegadas (RFC 3550).
// Os números alimentam o StatsSummary (mesmas métricas do TS) e o controle de banda.

#include <rtc/rtc.hpp>

#include <atomic>
#include <chrono>
#include <mutex>

namespace openspeak {

struct RtpCounters {
    // envio
    uint64_t packetsSent = 0, bytesSent = 0;
    uint64_t nackedPackets = 0, nackedBytes = 0; // estimativa de retransmissões
    double fractionLost = 0;                     // do último RR (0..1)
    int64_t cumulativeLost = 0;
    double jitter = 0;           // s
    double rtt = 0;              // s (último)
    double rttSum = 0;
    uint64_t rttCount = 0;
    uint32_t rembBitrate = 0;    // bps (0 = nenhum)
    uint64_t receiverReports = 0;
    // recebimento
    uint64_t packetsReceived = 0, bytesReceived = 0;
    int64_t packetsLost = 0;
};

class RtpStatsHandler final : public rtc::MediaHandler {
public:
    RtpStatsHandler(uint32_t clockRate, bool sender);

    void incoming(rtc::message_vector &messages, const rtc::message_callback &send) override;
    void outgoing(rtc::message_vector &messages, const rtc::message_callback &send) override;

    RtpCounters snapshot() const;

private:
    void onRtcp(const std::byte *data, size_t size);
    void onRtp(const std::byte *data, size_t size);

    const uint32_t m_clockRate;
    const bool m_sender;
    mutable std::mutex m_mutex;
    RtpCounters m_c;
    uint32_t m_ssrc = 0;

    // estado de recebimento
    bool m_haveSeq = false;
    uint16_t m_maxSeq = 0;
    uint32_t m_cycles = 0;
    uint32_t m_baseSeq = 0;
    double m_transit = 0;
    bool m_haveTransit = false;
    std::chrono::steady_clock::time_point m_epoch = std::chrono::steady_clock::now();
};

/// NTP "middle 32 bits" de agora (para RTT via LSR/DLSR).
uint32_t ntpMiddle32Now();

} // namespace openspeak

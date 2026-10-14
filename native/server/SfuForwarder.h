#pragma once
// Núcleo do relay SFU de um stream: recebe o RTP do transmissor (ingest) e
// repassa a cada espectador (egress), sem decodificar nem transcodificar.
//
// Roda nas threads da libdatachannel; todo o estado é protegido por mutex.
// Reescrita no repasse (uma vez por pacote, igual para todos os espectadores):
//  - payload type fixo do lado de saída (H.264 = 96, Opus = 111);
//  - header extensions removidas: os IDs de extmap negociados pelo transmissor
//    não valem na sessão com o espectador;
//  - seq e timestamp contínuos: quando o transmissor reconecta ele volta com
//    SSRC/seq/timestamp novos, e os offsets mantêm a sequência de saída sem
//    saltos (o decodificador do espectador nem percebe, só pede um keyframe).
// Cada espectador só troca o SSRC pelo da própria track (EgressSink).

#include <rtc/rtc.hpp>

#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace openspeak::sfu {

enum class Kind { Video = 0, Audio = 1 };

constexpr uint8_t VideoPayloadType = 96;
constexpr uint8_t AudioPayloadType = 111;
constexpr uint32_t VideoClockRate = 90000;
constexpr uint32_t AudioClockRate = 48000;
/// Constrained Baseline nível 5.2 (o mesmo do SessionOutbound do cliente).
inline constexpr const char *H264Profile = "profile-level-id=42e034;packetization-mode=1;level-asymmetry-allowed=1";

/// Tracks de saída de um espectador (trocam a cada reconexão da sessão).
/// `send` é virtual para o espectador por WebSocket (SfuWsViewer).
class EgressSink {
public:
    virtual ~EgressSink() = default;
    void attach(std::shared_ptr<rtc::Track> video, std::shared_ptr<rtc::Track> audio, uint32_t videoSsrc, uint32_t audioSsrc);
    void detach();
    /// Copia o pacote já reescrito, aplica o SSRC deste espectador e envia.
    virtual void send(Kind kind, const rtc::binary &packet);
    uint64_t sentBytes() const { return m_sentBytes.load(); }

private:
    std::mutex m_mutex;
    std::shared_ptr<rtc::Track> m_video, m_audio;
    uint32_t m_videoSsrc = 0, m_audioSsrc = 0;
    std::atomic<uint64_t> m_sentBytes{0};
};

class Forwarder {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto KeyFrameInterval = std::chrono::milliseconds(500);

    // --- lado do transmissor (ingest)
    void setIngestVideoTrack(const std::shared_ptr<rtc::Track> &track);
    void onRtp(Kind kind, const std::byte *data, size_t size);
    uint64_t receivedBytes() const { return m_receivedBytes.load(); }
    /// Sender Report do transmissor: âncora NTP<->RTP para o SR de saída.
    void onSenderReport(Kind kind, uint32_t ssrc, uint32_t rtpTimestamp);

    // --- lado dos espectadores (egress)
    void addSink(const std::shared_ptr<EgressSink> &sink);
    void removeSink(const std::shared_ptr<EgressSink> &sink);
    /// PLI ao transmissor, no máximo um a cada KeyFrameInterval (os pedidos
    /// dentro da janela são agregados e saem quando ela abre).
    void requestKeyFrame();
    /// REMB ao transmissor (controle de banda agregado dos espectadores).
    void requestBitrate(unsigned int bitrate);
    /// Timestamp RTP de saída correspondente a "agora", para o Sender Report
    /// do egress (mantém áudio e vídeo na mesma referência: lip-sync).
    std::optional<uint32_t> rtpNow(Kind kind) const;

private:
    struct Lane {
        bool started = false;    // já repassou algum pacote
        uint32_t srcSsrc = 0;
        uint16_t seqOffset = 0;
        uint32_t tsOffset = 0;
        uint16_t lastSeq = 0;    // último seq de saída (o mais novo)
        uint32_t lastTs = 0;
        Clock::time_point lastAt;
        bool haveSr = false;     // SR do SSRC atual
        uint32_t srRtp = 0;
        Clock::time_point srAt;
    };
    static uint32_t clockRate(Kind kind) { return kind == Kind::Video ? VideoClockRate : AudioClockRate; }
    /// Decide (sob o mutex) se um PLI pendente pode sair agora.
    std::shared_ptr<rtc::Track> takeKeyFrameRequestLocked(Clock::time_point now);

    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_receivedBytes{0};
    Lane m_lanes[2];
    std::vector<std::shared_ptr<EgressSink>> m_sinks;
    std::weak_ptr<rtc::Track> m_ingestVideo;
    bool m_keyFramePending = false;
    Clock::time_point m_lastKeyFrameRequest;
};

} // namespace openspeak::sfu

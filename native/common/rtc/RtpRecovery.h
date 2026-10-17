#pragma once
// Recuperação de perda de pacotes RTP de vídeo H.264 no lado que recebe.
//
// Sem isto, o pacote perdido nunca é reenviado (ninguém manda NACK) e o
// depacketizador entrega o quadro pela metade: o decodificador "inventa" o
// buraco e o erro se espalha pelos quadros seguintes até o próximo keyframe.
//
//  - NACK: cada lacuna na sequência vira um NACK genérico (RFC 4585) na hora,
//    repetido a cada ~RTT até o pacote chegar ou o prazo vencer. O RTT é
//    medido pelo próprio reenvio (NACK -> pacote).
//  - Modo Frames (espectador): reordena e só entrega quadros completos ao
//    depacketizador. Vencido o prazo de um pacote, o quadro é descartado e
//    tudo é descartado até o próximo keyframe (com PLI): a imagem congela um
//    instante em vez de mostrar pixels corrompidos.
//  - Modo PassThrough (ingest do SFU): repassa tudo na hora, só pede os
//    reenvios; quem recebe do SFU recebe o pacote recuperado em seguida.

#include <rtc/rtc.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace openspeak {

struct RtpRecoveryStats {
    uint64_t nackedPackets = 0;   // pacotes pedidos (primeiro NACK de cada um)
    uint64_t recoveredPackets = 0; // chegaram depois do NACK
    uint64_t lostPackets = 0;     // prazo vencido
    uint64_t droppedFrames = 0;   // quadros descartados (incompletos ou à espera do keyframe)
    uint64_t keyFrameRequests = 0;
};

/// Lógica pura (sem MediaHandler): recebe pacotes RTP e devolve o que sai,
/// os números de sequência a pedir e se é preciso um keyframe.
class RtpRecoveryBuffer {
public:
    using Clock = std::chrono::steady_clock;
    enum class Mode { Frames, PassThrough };

    struct Events {
        std::vector<uint16_t> nack;
        bool keyFrame = false;
        uint32_t mediaSsrc = 0;
    };

    explicit RtpRecoveryBuffer(Mode mode) : m_mode(mode) {}

    /// Um pacote RTP chegou. Os pacotes prontos vão para `out`.
    void push(rtc::message_ptr packet, Clock::time_point now, rtc::message_vector &out, Events &ev);
    /// Reenvia NACKs e aplica os prazos; chamado a cada lote recebido.
    void poll(Clock::time_point now, rtc::message_vector &out, Events &ev);

    RtpRecoveryStats stats() const { return m_stats; }
    Clock::duration rtt() const { return m_srtt; }

    /// Prazos em função do RTT medido.
    Clock::duration retryInterval() const;
    Clock::duration giveUpAfter() const;

    static constexpr int MaxNacksPerPacket = 6;
    static constexpr size_t MaxBuffered = 2000;
    static constexpr int64_t MaxGap = 1000; // acima disso é descontinuidade, não perda
    static constexpr auto KeyFrameRetry = std::chrono::milliseconds(1000);

private:
    struct Packet {
        rtc::message_ptr message;
        uint32_t timestamp = 0;
        bool marker = false;
        bool hasSps = false; // início de keyframe (SPS)
        bool hasIdr = false;
    };
    struct Missing {
        Clock::time_point detected;
        Clock::time_point lastNack;
        int nacks = 0;
    };

    int64_t unwrap(uint16_t seq) const;
    void reset(int64_t ext, Clock::time_point now, Events &ev);
    void emitFrames(rtc::message_vector &out);
    void giveUp(Clock::time_point now, Events &ev);
    void requestKeyFrame(Clock::time_point now, Events &ev, bool force);

    const Mode m_mode;
    bool m_started = false;
    uint32_t m_ssrc = 0;
    int64_t m_highest = 0; // maior sequência estendida vista
    int64_t m_next = 0;    // próxima a entregar (modo Frames)
    std::map<int64_t, Packet> m_packets;
    std::map<int64_t, Missing> m_missing;
    bool m_aligned = false; // m_next é início de quadro
    bool m_waitKey = true;
    Clock::time_point m_lastKeyRequest{};
    Clock::duration m_srtt = std::chrono::milliseconds(50);
    RtpRecoveryStats m_stats;
};

/// MediaHandler para a cadeia de recebimento da track (antes do depacketizador).
class RtpRecoveryHandler final : public rtc::MediaHandler {
public:
    explicit RtpRecoveryHandler(RtpRecoveryBuffer::Mode mode) : m_buffer(mode) {}

    void incoming(rtc::message_vector &messages, const rtc::message_callback &send) override;

    RtpRecoveryStats stats() const;

private:
    mutable std::mutex m_mutex;
    RtpRecoveryBuffer m_buffer;
};

/// NACK genérico (RTCP 205/FMT 1) agrupando os números em PID + bitmask.
rtc::binary buildGenericNack(uint32_t senderSsrc, uint32_t mediaSsrc, std::vector<uint16_t> seqs);
/// PLI (RTCP 206/FMT 1).
rtc::binary buildPli(uint32_t senderSsrc, uint32_t mediaSsrc);

} // namespace openspeak

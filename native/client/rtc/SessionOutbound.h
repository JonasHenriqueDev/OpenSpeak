#pragma once
// Sessão transmissor -> um espectador (com::teamspeak::video::SessionOutbound).
// Não codifica nada: recebe os quadros já codificados do encoder
// compartilhado e só empacota em RTP (single-encoding).

#include "RtcSession.h"
#include "RtpStatsHandler.h"
#include "StreamStats.h"
#include "VideoEncoder.h"

#include <mutex>

namespace openspeak {

class SessionOutbound : public RtcSession {
    Q_OBJECT
public:
    SessionOutbound(QString streamId, int peerClid, QJsonArray iceServers, QStringList remoteCodecs, SignalFn send,
                    std::function<void()> requestKeyFrame, QObject *parent = nullptr);

    void start() override;

    /// Thread-safe (chamado pela thread do encoder / da captura de áudio).
    void sendVideo(const EncodedVideoFrame &frame);
    void sendAudio(const QByteArray &opus, quint32 rtpTimestamp);

    bool remoteSupportsH264() const;
    QString activeCodec() const { return QStringLiteral("video/H264"); }

    /// Atualiza estatísticas e a estimativa de banda deste espectador (1 Hz).
    void collectStats(const VideoEncoderStats &enc, const TargetQuality &target, int configuredBitrate);
    const StatsSummary &videoStats() const { return m_video; }
    const StatsSummary &audioStats() const { return m_audio; }
    /// Bitrate suportado por este espectador (controle de congestionamento).
    int estimatedBitrate() const { return m_estimate; }

protected:
    void setupPeerConnection(rtc::PeerConnection &pc) override;
    QString transformLocalSdp(const QString &sdp) override;

private:
    struct Media {
        std::shared_ptr<rtc::Track> video, audio;
        std::shared_ptr<RtpStatsHandler> videoStats, audioStats;
    };

    QStringList m_remoteCodecs;
    std::function<void()> m_requestKeyFrame;
    mutable std::mutex m_mediaMutex;
    Media m_media;
    uint32_t m_videoSsrc = 0, m_audioSsrc = 0;

    StatsSummary m_video, m_audio;
    RtpCounters m_prevVideo, m_prevAudio;
    qint64 m_prevAt = 0;
    int m_estimate = 0;
};

} // namespace openspeak

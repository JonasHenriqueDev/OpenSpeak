#pragma once
// Sessão espectador <- transmissor (com::teamspeak::video::SessionInbound).
// Desempacota H.264/Opus, decodifica em threads próprias e entrega quadros
// ao VideoOutput da QML e PCM ao AudioPlayer.

#include "AudioCodec.h"
#include "AudioPlayer.h"
#include "RtcSession.h"
#include "RtpStatsHandler.h"
#include "StreamStats.h"
#include "VideoDecoder.h"

#include <QList>
#include <QPointer>
#include <QVideoSink>

#include <mutex>

namespace openspeak {

class SessionInbound : public RtcSession {
    Q_OBJECT
public:
    SessionInbound(QString streamId, int peerClid, QJsonArray iceServers, SignalFn send, QObject *parent = nullptr);
    ~SessionInbound() override;

    void start() override;

    void addVideoSink(QVideoSink *sink);
    void removeVideoSink(QVideoSink *sink);
    void setVolume(double v) { m_player.setVolume(v); }
    double volume() const { return m_player.volume(); }
    bool hasVideo() const { return m_hasVideo; }

    /// Supported decode codecs (sent in joinstreamrequest).
    static QStringList localDecodeCodecs() { return {QStringLiteral("video/H264")}; }

    void collectStats(const TargetQuality *target);
    const StatsSummary &videoStats() const { return m_video; }
    const StatsSummary &audioStats() const { return m_audio; }

signals:
    void firstFrame();

protected:
    void setupPeerConnection(rtc::PeerConnection &) override {}
    void onRemoteTrack(std::shared_ptr<rtc::Track> track) override;
    QString transformRemoteSdp(const QString &sdp) override;
    QString transformLocalSdp(const QString &sdp) override;

private:
    void deliverFrame(const QVideoFrame &frame);
    void requestKeyFrame();

    VideoDecoder m_decoder;
    std::unique_ptr<OpusDecoder> m_opus;
    AudioPlayer m_player;
    QList<QPointer<QVideoSink>> m_sinks;
    QVideoFrame m_lastFrame;
    bool m_hasVideo = false;
    QTimer m_offerTimer;

    std::mutex m_trackMutex;
    std::shared_ptr<rtc::Track> m_videoTrack, m_audioTrack;
    std::shared_ptr<RtpStatsHandler> m_videoStats, m_audioStats;

    StatsSummary m_video, m_audio;
    RtpCounters m_prevVideo, m_prevAudio;
    qint64 m_prevAt = 0;
};

} // namespace openspeak

#include "SessionInbound.h"
#include "RtpRecovery.h"
#include "SdpFilter.h"

#include <QDateTime>

namespace openspeak {

namespace {
constexpr int OfferWaitMs = 10'000;
}

SessionInbound::SessionInbound(QString streamId, int peerClid, QJsonArray iceServers, SignalFn send, QObject *parent)
    : RtcSession(std::move(streamId), peerClid, std::move(iceServers), true, std::move(send), parent)
{
    m_video.kind = QStringLiteral("video");
    m_audio.kind = QStringLiteral("audio");
    m_offerTimer.setSingleShot(true);
    connect(&m_offerTimer, &QTimer::timeout, this, [this] {
        if (m_pc && !m_pc->remoteDescription())
            reconnect();
    });
}

SessionInbound::~SessionInbound()
{
    {
        std::lock_guard lock(m_trackMutex);
        for (const auto &track : {m_videoTrack, m_audioTrack})
            if (track)
                track->resetCallbacks();
    }
    close(); // derruba a PeerConnection antes dos decodificadores
    m_decoder.stop();
}

void SessionInbound::start()
{
    QPointer<SessionInbound> self(this);
    m_decoder.start(
        QStringLiteral("H264"),
        [self](const QVideoFrame &frame) {
            QMetaObject::invokeMethod(self, [self, frame] {
                if (self)
                    self->deliverFrame(frame);
            }, Qt::QueuedConnection);
        },
        [self] {
            QMetaObject::invokeMethod(self, [self] {
                if (self)
                    self->requestKeyFrame();
            }, Qt::QueuedConnection);
        });
    createPeerConnection();
    m_offerTimer.start(OfferWaitMs);
}

QString SessionInbound::transformRemoteSdp(const QString &sdp)
{
    return sdp::filterCodecs(sdp, {QStringLiteral("H264")}, {QStringLiteral("opus")});
}

QString SessionInbound::transformLocalSdp(const QString &sdp) { return sdp::opusStereo(sdp, 510'000); }

void SessionInbound::onRemoteTrack(std::shared_ptr<rtc::Track> track)
{
    // Thread do libdatachannel.
    const auto desc = track->description();
    auto session = std::make_shared<rtc::RtcpReceivingSession>(); // RR + PLI
    const int gen = m_generation;
    QPointer<SessionInbound> self(this);

    if (desc.type() == "video") {
        auto stats = std::make_shared<RtpStatsHandler>(90000, false);
        auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::LongStartSequence);
        // Pede reenvio do que se perdeu e só deixa passar quadros completos.
        auto recovery = std::make_shared<RtpRecoveryHandler>(RtpRecoveryBuffer::Mode::Frames);
        // A entrada percorre a cadeia do fim para o início: sessão RTCP e
        // estatísticas veem o RTP como chegou, a recuperação reordena e o
        // depacketizador monta os quadros.
        track->setMediaHandler(depacketizer);
        depacketizer->addToChain(recovery);
        depacketizer->addToChain(stats);
        depacketizer->addToChain(session);
        VideoDecoder *decoder = &m_decoder;
        track->onFrame([decoder, self](rtc::binary data, rtc::FrameInfo) {
            if (self) // o decodificador vive enquanto a sessão existir
                decoder->push(QByteArray(reinterpret_cast<const char *>(data.data()), qsizetype(data.size())));
        });
        track->onOpen([self, gen] {
            QMetaObject::invokeMethod(self, [self, gen] {
                if (self && self->m_generation == gen)
                    self->requestKeyFrame();
            }, Qt::QueuedConnection);
        });
        std::lock_guard lock(m_trackMutex);
        m_videoTrack = track;
        m_videoStats = stats;
    } else if (desc.type() == "audio") {
        auto stats = std::make_shared<RtpStatsHandler>(48000, false);
        auto depacketizer = std::make_shared<rtc::OpusRtpDepacketizer>();
        track->setMediaHandler(depacketizer);
        depacketizer->addToChain(stats);
        depacketizer->addToChain(session);
        auto opus = std::make_shared<OpusDecoder>();
        track->onFrame([opus, self](rtc::binary data, rtc::FrameInfo) {
            QByteArray pcm = opus->decode(QByteArray(reinterpret_cast<const char *>(data.data()), qsizetype(data.size())));
            if (pcm.isEmpty())
                return;
            QMetaObject::invokeMethod(self, [self, pcm = std::move(pcm)] {
                if (self)
                    self->m_player.write(pcm);
            }, Qt::QueuedConnection);
        });
        std::lock_guard lock(m_trackMutex);
        m_audioTrack = track;
        m_audioStats = stats;
    }
}

void SessionInbound::requestKeyFrame()
{
    std::shared_ptr<rtc::Track> track;
    {
        std::lock_guard lock(m_trackMutex);
        track = m_videoTrack;
    }
    if (track && track->isOpen())
        track->requestKeyframe();
}

void SessionInbound::addVideoSink(QVideoSink *sink)
{
    if (!sink || m_sinks.contains(sink))
        return;
    m_sinks.append(sink);
    if (m_lastFrame.isValid())
        sink->setVideoFrame(m_lastFrame);
}

void SessionInbound::removeVideoSink(QVideoSink *sink) { m_sinks.removeAll(sink); }

void SessionInbound::deliverFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    m_lastFrame = frame;
    m_sinks.removeAll(nullptr);
    for (const auto &sink : std::as_const(m_sinks))
        sink->setVideoFrame(frame);
    if (!m_hasVideo) {
        m_hasVideo = true;
        emit firstFrame();
    }
}

void SessionInbound::collectStats(const TargetQuality *target)
{
    std::shared_ptr<RtpStatsHandler> vs, as;
    {
        std::lock_guard lock(m_trackMutex);
        vs = m_videoStats;
        as = m_audioStats;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_prevAt ? (now - m_prevAt) / 1000.0 : 0;
    m_prevAt = now;
    const QString connType = connectionType();

    auto fill = [&](StatsSummary &s, const std::shared_ptr<RtpStatsHandler> &h, RtpCounters &prev) {
        if (!h)
            return;
        const RtpCounters c = h->snapshot();
        s.received.packets = qint64(c.packetsReceived);
        s.received.bytes = qint64(c.bytesReceived);
        s.received.bps = dt > 0 ? (c.bytesReceived - prev.bytesReceived) * 8 / dt : 0;
        s.packetsLost = c.packetsLost;
        const double dLost = double(c.packetsLost - prev.packetsLost);
        const double dRecv = double(c.packetsReceived - prev.packetsReceived);
        s.fractionLost = dLost + dRecv > 0 ? std::max(0.0, dLost / (dLost + dRecv)) : 0;
        s.jitter = c.jitter;
        s.connectionType = connType;
        s.srtpCipher = QStringLiteral("SRTP (DTLS)");
        prev = c;
    };
    fill(m_video, vs, m_prevVideo);
    fill(m_audio, as, m_prevAudio);

    const VideoDecoderStats d = m_decoder.stats();
    m_video.codec = QStringLiteral("video/H264");
    m_audio.codec = QStringLiteral("audio/opus");
    m_video.video.hasDecode = true;
    m_video.video.decoder = d.decoder;
    m_video.video.width = d.width;
    m_video.video.height = d.height;
    m_video.video.fps = int(std::lround(d.fps));
    m_video.video.avgDecodeTime = d.avgDecodeTime;
    analyzeIssues(m_video, target, false);
    analyzeIssues(m_audio, target, false);
}

} // namespace openspeak

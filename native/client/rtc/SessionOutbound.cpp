#include "SessionOutbound.h"
#include "SdpFilter.h"

#include <QDateTime>
#include <QRandomGenerator>

namespace openspeak {

namespace {
constexpr int VideoPayloadType = 96;
constexpr int AudioPayloadType = 111;
// Constrained Baseline nível 5.2: aceito por qualquer navegador, até 4K.
const char *const H264Profile = "profile-level-id=42e034;packetization-mode=1;level-asymmetry-allowed=1";
constexpr int MinBitrate = 150'000;
} // namespace

SessionOutbound::SessionOutbound(QString streamId, int peerClid, QJsonArray iceServers, QStringList remoteCodecs, SignalFn send,
                                 std::function<void()> requestKeyFrame, QObject *parent)
    : RtcSession(std::move(streamId), peerClid, std::move(iceServers), false, std::move(send), parent),
      m_remoteCodecs(std::move(remoteCodecs)), m_requestKeyFrame(std::move(requestKeyFrame))
{
    m_video.kind = QStringLiteral("video");
    m_audio.kind = QStringLiteral("audio");
    m_video.codec = activeCodec();
    m_audio.codec = QStringLiteral("audio/opus");
}

bool SessionOutbound::remoteSupportsH264() const
{
    // Lista vazia = cliente antigo / desconhecido: tenta mesmo assim.
    return m_remoteCodecs.isEmpty() || m_remoteCodecs.contains(QStringLiteral("video/H264"), Qt::CaseInsensitive);
}

void SessionOutbound::start()
{
    createPeerConnection();
    sendOffer(false);
}

void SessionOutbound::setupPeerConnection(rtc::PeerConnection &pc)
{
    const std::string cname = "openspeak";
    const std::string msid = "openspeak-" + streamId().toStdString();
    auto *rng = QRandomGenerator::global();
    m_videoSsrc = rng->generate() | 1;
    m_audioSsrc = rng->generate() | 1;
    auto keyFrame = m_requestKeyFrame;

    rtc::Description::Video vdesc("video", rtc::Description::Direction::SendOnly);
    vdesc.addH264Codec(VideoPayloadType, H264Profile);
    vdesc.addSSRC(m_videoSsrc, cname, msid, "video");
    auto video = pc.addTrack(vdesc);
    auto vcfg = std::make_shared<rtc::RtpPacketizationConfig>(m_videoSsrc, cname, VideoPayloadType,
                                                              rtc::H264RtpPacketizer::ClockRate);
    auto vpack = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, vcfg);
    auto vstats = std::make_shared<RtpStatsHandler>(rtc::H264RtpPacketizer::ClockRate, true);
    vpack->addToChain(std::make_shared<rtc::RtcpSrReporter>(vcfg));
    vpack->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    vpack->addToChain(std::make_shared<rtc::PliHandler>([keyFrame] { keyFrame(); })); // PLI do espectador
    vpack->addToChain(vstats);
    video->setMediaHandler(vpack);
    video->onOpen([keyFrame] { keyFrame(); }); // novo espectador começa num keyframe

    rtc::Description::Audio adesc("audio", rtc::Description::Direction::SendOnly);
    adesc.addOpusCodec(AudioPayloadType);
    adesc.addSSRC(m_audioSsrc, cname, msid, "audio");
    auto audio = pc.addTrack(adesc);
    auto acfg = std::make_shared<rtc::RtpPacketizationConfig>(m_audioSsrc, cname, AudioPayloadType,
                                                              rtc::OpusRtpPacketizer::DefaultClockRate);
    auto apack = std::make_shared<rtc::OpusRtpPacketizer>(acfg);
    auto astats = std::make_shared<RtpStatsHandler>(rtc::OpusRtpPacketizer::DefaultClockRate, true);
    apack->addToChain(std::make_shared<rtc::RtcpSrReporter>(acfg));
    apack->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    apack->addToChain(astats);
    audio->setMediaHandler(apack);

    std::lock_guard lock(m_mediaMutex);
    m_media = {video, audio, vstats, astats};
    m_prevVideo = {};
    m_prevAudio = {};
}

QString SessionOutbound::transformLocalSdp(const QString &sdp) { return sdp::opusStereo(sdp, 510'000); }

void SessionOutbound::sendVideo(const EncodedVideoFrame &frame)
{
    std::shared_ptr<rtc::Track> track;
    {
        std::lock_guard lock(m_mediaMutex);
        track = m_media.video;
    }
    if (!track || !track->isOpen())
        return;
    try {
        track->sendFrame(reinterpret_cast<const std::byte *>(frame.data.constData()), size_t(frame.data.size()),
                         rtc::FrameInfo(std::chrono::duration<double>(frame.timestampUs / 1e6)));
    } catch (const std::exception &) {
        // track fechando durante uma reconexão
    }
}

void SessionOutbound::sendAudio(const QByteArray &opus, quint32 rtpTimestamp)
{
    std::shared_ptr<rtc::Track> track;
    {
        std::lock_guard lock(m_mediaMutex);
        track = m_media.audio;
    }
    if (!track || !track->isOpen())
        return;
    try {
        track->sendFrame(reinterpret_cast<const std::byte *>(opus.constData()), size_t(opus.size()),
                         rtc::FrameInfo(std::chrono::duration<double>(rtpTimestamp / 48000.0)));
    } catch (const std::exception &) {
    }
}

void SessionOutbound::collectStats(const VideoEncoderStats &enc, const TargetQuality &target, int configuredBitrate)
{
    Media media;
    {
        std::lock_guard lock(m_mediaMutex);
        media = m_media;
    }
    if (!media.videoStats)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_prevAt ? (now - m_prevAt) / 1000.0 : 0;
    m_prevAt = now;
    const QString connType = connectionType();

    auto fill = [&](StatsSummary &s, const RtpCounters &c, RtpCounters &prev) {
        s.sent.packets = qint64(c.packetsSent);
        s.sent.bytes = qint64(c.bytesSent);
        s.sent.bps = dt > 0 ? (c.bytesSent - prev.bytesSent) * 8 / dt : 0;
        s.retransmit.packets = qint64(c.nackedPackets);
        s.retransmit.bytes = qint64(c.nackedBytes);
        s.retransmit.bps = dt > 0 ? (c.nackedBytes - prev.nackedBytes) * 8 / dt : 0;
        s.fractionLost = c.fractionLost;
        s.packetsLost = std::max<int64_t>(0, c.cumulativeLost);
        s.jitter = c.jitter;
        s.lastRoundTripTime = c.rtt;
        s.avgRoundTripTime = c.rttCount ? c.rttSum / double(c.rttCount) : 0;
        s.connectionType = connType;
        s.srtpCipher = QStringLiteral("SRTP (DTLS)");
        prev = c;
    };

    const RtpCounters vc = media.videoStats->snapshot();
    const bool newReport = vc.receiverReports != m_prevVideo.receiverReports;
    fill(m_video, vc, m_prevVideo);
    fill(m_audio, media.audioStats->snapshot(), m_prevAudio);

    // Controle de congestionamento por espectador (AIMD sobre a perda do RR,
    // limitado pelo REMB quando o receptor envia).
    if (m_estimate <= 0)
        m_estimate = configuredBitrate;
    if (newReport) {
        const double loss = vc.fractionLost;
        if (loss > 0.10)
            m_estimate = int(m_estimate * (1.0 - 0.5 * loss));
        else if (loss < 0.02)
            m_estimate = int(m_estimate * 1.05);
    }
    if (vc.rembBitrate > 0)
        m_estimate = std::min<int>(m_estimate, int(vc.rembBitrate));
    m_estimate = std::clamp(m_estimate, MinBitrate, configuredBitrate);

    m_video.targetBitrate = m_estimate;
    m_video.video.hasEncode = true;
    m_video.video.encoder = enc.encoder;
    m_video.video.width = enc.width;
    m_video.video.height = enc.height;
    m_video.video.fps = int(std::lround(enc.fps));
    m_video.video.avgEncodeTime = enc.avgEncodeTime;
    m_video.video.limitReason = enc.limitReason;
    analyzeIssues(m_video, &target, true);
    analyzeIssues(m_audio, &target, false);
}

} // namespace openspeak

#pragma once
// Sessão transmissor -> servidor (papel inbound: o transmissor oferta).
// Não depacketiza nada: aceita só H.264 + Opus, responde RR/PLI/REMB com a
// RtcpReceivingSession e entrega o RTP cru ao Forwarder do stream.

#include "RtcSession.h"
#include "SfuForwarder.h"

#include <mutex>

namespace openspeak {

class SfuIngest : public RtcSession {
    Q_OBJECT
public:
    SfuIngest(QString streamId, int ownerClid, QJsonArray iceServers, SignalFn send,
              std::shared_ptr<sfu::Forwarder> forwarder, QObject *parent = nullptr);
    ~SfuIngest() override;

    void start() override;

protected:
    void setupPeerConnection(rtc::PeerConnection &pc) override;
    void onRemoteTrack(std::shared_ptr<rtc::Track> track) override;
    QString transformRemoteSdp(const QString &sdp) override;
    QString transformLocalSdp(const QString &sdp) override;

private:
    class Tap;
    void retireTaps();

    std::shared_ptr<sfu::Forwarder> m_forwarder;
    std::mutex m_trackMutex;
    std::vector<std::shared_ptr<rtc::Track>> m_tracks; // PeerConnection atual
    std::vector<std::shared_ptr<Tap>> m_taps;
};

} // namespace openspeak

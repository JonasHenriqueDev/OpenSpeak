#pragma once
// Relay de mídia do servidor (STREAM_SFU): por stream, uma SfuIngest com o
// transmissor, um Forwarder e uma SfuEgress por espectador.
//
// Cenário alvo: 1 transmissor, poucos espectadores (~4), uma qualidade só
// (sem simulcast) e codec fixo H.264 + Opus, repassado sem transcodificar.

#include "RtcSession.h"
#include "StreamRelay.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QTimer>

#include <map>
#include <memory>

namespace openspeak {

class SfuEgress;
class SfuIngest;
class SfuWsViewer;
namespace sfu { class Forwarder; }

class SfuRelay : public QObject, public StreamRelay {
    Q_OBJECT
public:
    struct Options {
        QJsonArray iceServers;          // STUN do próprio servidor (OPENSPEAK_ICE)
        RtcSession::Transport transport; // porta UDP única + IP público anunciado
    };

    explicit SfuRelay(Options options, QObject *parent = nullptr);
    ~SfuRelay() override;

    void setCallbacks(SignalFn signal, FailFn failed) override;
    void openStream(const QString &streamId, int ownerClid, int bitrate) override;
    void updateStream(const QString &streamId, int bitrate) override;
    void closeStream(const QString &streamId) override;
    void addViewer(const QString &streamId, int clid) override;
    bool addWebSocketViewer(const QString &streamId, int clid) override;
    void setBinarySender(BinaryFn send) override;
    void removeViewer(const QString &streamId, int clid) override;
    void signaling(const QString &streamId, int fromClid, const QJsonObject &payload) override;
    void resumePeer(int clid) override;
    QJsonArray monitoringSessions() override;

    /// Piso do bitrate pedido ao transmissor: um espectador ruim não derruba
    /// a qualidade de todos abaixo disso.
    static constexpr int MinTargetBitrate = 300'000;

private:
    struct Stream {
        QString id;
        int ownerClid = 0;
        int bitrate = 0;
        std::shared_ptr<sfu::Forwarder> forwarder;
        SfuIngest *ingest = nullptr;
        std::map<int, SfuEgress *> viewers;
        std::map<int, SfuWsViewer *> wsViewers; // fora do controle de banda (sem RR/REMB)
    };

    template <typename Session> void wire(Session *session, const QString &streamId, int clid);
    void dispose(QObject *session);
    void adjustBitrates();

    Options m_options;
    SignalFn m_signal;
    FailFn m_failed;
    BinaryFn m_binary;
    std::map<QString, Stream> m_streams;
    QTimer m_statsTimer;
};

} // namespace openspeak

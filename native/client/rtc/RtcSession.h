#pragma once
// Base de uma sessão P2P (com::teamspeak::video::StreamBase), sobre libdatachannel.
//
// Sinalização via `streamsignaling`, mesmo payload do cliente web e do TS:
//   {type:'offer', sdp, isReconnect} | {type:'answer', sdp}
//   {type:'ice', candidate:{candidate, sdpMid}} | {type:'reconnect'}
// O transmissor sempre oferta. Reconexão = PeerConnection nova nos dois lados
// (libdatachannel não faz ICE restart), anunciada com isReconnect.

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <rtc/rtc.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace openspeak {

class RtcSession : public QObject {
    Q_OBJECT
public:
    enum class State { New, Connecting, Connected, Reconnecting, Failed, Closed };
    Q_ENUM(State)

    using SignalFn = std::function<void(const QJsonObject &payload)>;

    RtcSession(QString streamId, int peerClid, QJsonArray iceServers, bool inbound, SignalFn send, QObject *parent = nullptr);
    ~RtcSession() override;

    virtual void start() = 0;
    void onSignalingMessage(const QJsonObject &payload);
    void close();
    void setBindAddress(const QString &address) { m_bindAddress = address; }

    QString streamId() const { return m_streamId; }
    int peerClid() const { return m_peerClid; }
    bool isInbound() const { return m_inbound; }
    State state() const { return m_state; }
    QString stateName() const;
    /// host | srflx | relay (par de candidatos selecionado)
    QString connectionType() const;

signals:
    void stateChanged(openspeak::RtcSession::State state);
    void failed();

protected:
    /// Cria uma PeerConnection nova (descarta a anterior).
    void createPeerConnection();
    /// Outbound: adiciona as tracks. Inbound: nada (as tracks vêm na oferta).
    virtual void setupPeerConnection(rtc::PeerConnection &pc) = 0;
    /// Inbound: chamado na thread do libdatachannel para configurar a track.
    virtual void onRemoteTrack(std::shared_ptr<rtc::Track>) {}
    /// Ajuste do SDP local antes de enviar (ex.: Opus estéreo).
    virtual QString transformLocalSdp(const QString &sdp) { return sdp; }
    /// Ajuste do SDP remoto antes de aplicar (ex.: filtrar codecs do navegador).
    virtual QString transformRemoteSdp(const QString &sdp) { return sdp; }
    virtual void onConnected() {}

    void sendOffer(bool isReconnect);
    void reconnect();
    void fail(const QString &why);
    void setState(State s);
    /// Executa `fn` na thread do Qt se a geração da PeerConnection ainda for a atual.
    void post(int generation, std::function<void()> fn);

    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::atomic<int> m_generation{0};

private:
    void onOffer(const QJsonObject &payload);
    void onAnswer(const QJsonObject &payload);
    void onCandidate(const QJsonObject &payload);
    void addCandidate(const QJsonObject &candidate);
    void armConnectTimer();

    QString m_streamId;
    int m_peerClid = 0;
    QJsonArray m_iceServers;
    QString m_bindAddress;
    bool m_inbound = false;
    SignalFn m_send;
    State m_state = State::New;
    int m_reconnectAttempts = 0;
    bool m_reconnectPending = false;
    QList<QJsonObject> m_pendingCandidates;
    QTimer m_connectTimer;
    QTimer m_disconnectTimer;
};

} // namespace openspeak

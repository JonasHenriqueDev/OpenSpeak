#include "RtcSession.h"
#include "SdpFilter.h"

#include <QLoggingCategory>
#include <QMetaObject>

Q_LOGGING_CATEGORY(lcRtc, "openspeak.rtc")

namespace openspeak {

namespace {
constexpr int ConnectTimeoutMs = 20'000;
constexpr int DisconnectedGraceMs = 4'000;
constexpr int MaxReconnectAttempts = 5;

std::vector<rtc::IceServer> toIceServers(const QJsonArray &list)
{
    std::vector<rtc::IceServer> out;
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        QStringList urls;
        if (o.value("urls").isArray())
            for (const QJsonValue &u : o.value("urls").toArray())
                urls.append(u.toString());
        else
            urls.append(o.value("urls").toString());
        for (const QString &url : urls) {
            if (!url.startsWith(QLatin1String("stun:")) && !url.startsWith(QLatin1String("turn")))
                continue;
            try {
                rtc::IceServer server(url.toStdString());
                if (o.contains("username")) {
                    server.username = o.value("username").toString().toStdString();
                    server.password = o.value("credential").toString().toStdString();
                }
                out.push_back(std::move(server));
            } catch (const std::exception &e) {
                qCWarning(lcRtc) << "iceServer inválido" << url << e.what();
            }
        }
    }
    return out;
}
} // namespace

RtcSession::RtcSession(QString streamId, int peerClid, QJsonArray iceServers, bool inbound, SignalFn send, QObject *parent)
    : QObject(parent), m_streamId(std::move(streamId)), m_peerClid(peerClid), m_iceServers(std::move(iceServers)),
      m_inbound(inbound), m_send(std::move(send))
{
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Connecting || m_state == State::Reconnecting)
            reconnect();
    });
    m_disconnectTimer.setSingleShot(true);
    connect(&m_disconnectTimer, &QTimer::timeout, this, [this] {
        if (m_pc && m_pc->state() == rtc::PeerConnection::State::Disconnected)
            reconnect();
    });
}

RtcSession::~RtcSession() { close(); }

QString RtcSession::stateName() const
{
    switch (m_state) {
    case State::New: return QStringLiteral("new");
    case State::Connecting: return QStringLiteral("connecting");
    case State::Connected: return QStringLiteral("connected");
    case State::Reconnecting: return QStringLiteral("reconnecting");
    case State::Failed: return QStringLiteral("failed");
    case State::Closed: return QStringLiteral("closed");
    }
    return {};
}

QString RtcSession::connectionType() const
{
    if (!m_pc)
        return {};
    rtc::Candidate local, remote;
    if (!m_pc->getSelectedCandidatePair(&local, &remote))
        return {};
    using T = rtc::Candidate::Type;
    if (local.type() == T::Relayed || remote.type() == T::Relayed)
        return QStringLiteral("relay");
    if (local.type() == T::Host && remote.type() == T::Host)
        return QStringLiteral("host");
    return QStringLiteral("srflx");
}

QJsonObject RtcSession::monitoringSnapshot() const
{
    QJsonObject data{{"state", stateName()}, {"connectionType", connectionType()},
                     {"generation", m_generation.load()}};
    rtc::Candidate local, remote;
    if (m_pc && m_pc->getSelectedCandidatePair(&local, &remote)) {
        if (const auto address = remote.address())
            data["mediaIp"] = QString::fromStdString(*address);
        if (const auto port = remote.port())
            data["mediaPort"] = int(*port);
    }
    return data;
}

void RtcSession::post(int generation, std::function<void()> fn)
{
    QPointer<RtcSession> self(this);
    QMetaObject::invokeMethod(this, [self, generation, fn = std::move(fn)] {
        if (self && self->m_generation == generation && self->m_state != State::Closed)
            fn();
    }, Qt::QueuedConnection);
}

void RtcSession::createPeerConnection()
{
    if (m_pc) {
        m_pc->resetCallbacks();
        m_pc->close();
    }
    const int gen = ++m_generation;
    m_pendingCandidates.clear();
    m_announced.clear();

    rtc::Configuration config;
    config.iceServers = toIceServers(m_iceServers);
    if (!m_bindAddress.isEmpty()) {
        config.bindAddress = m_bindAddress.toStdString();
        config.iceServers.clear(); // tailnet transport needs neither STUN nor TURN
    }
    if (m_transport.udpMuxPort) {
        // Servidor: uma porta UDP só para todas as sessões (firewall/NAT simples).
        config.enableIceUdpMux = true;
        config.portRangeBegin = config.portRangeEnd = m_transport.udpMuxPort;
    }
    config.disableAutoNegotiation = true;
    config.forceMediaTransport = true;
    m_pc = std::make_shared<rtc::PeerConnection>(config);

    m_pc->onLocalDescription([this, gen](rtc::Description desc) {
        const QString type = QString::fromStdString(desc.typeString());
        const QString sdp = QString::fromStdString(std::string(desc));
        post(gen, [this, type, sdp] {
            const QString local = m_transport.publicIp.isEmpty() ? sdp : sdp::advertiseHostIp(sdp, m_transport.publicIp);
            QJsonObject payload{{"type", type}, {"sdp", transformLocalSdp(local)}};
            if (type == QLatin1String("offer"))
                payload["isReconnect"] = m_reconnectPending;
            m_reconnectPending = false;
            m_send(payload);
        });
    });
    m_pc->onLocalCandidate([this, gen](rtc::Candidate c) {
        QString candidate = QString::fromStdString(c.candidate());
        const QString mid = QString::fromStdString(c.mid());
        post(gen, [this, candidate, mid]() mutable {
            if (!m_transport.publicIp.isEmpty()) {
                // NAT 1:1 (ex.: VM na nuvem): o IP privado da interface não serve
                // para ninguém de fora; anuncia o público. Várias interfaces viram
                // o mesmo candidato, então descarta repetidos.
                candidate = sdp::advertiseHostIp(candidate, m_transport.publicIp);
                const QString key = sdp::candidateKey(candidate);
                if (m_announced.contains(key))
                    return;
                m_announced.insert(key);
            }
            m_send({{"type", "ice"}, {"candidate", QJsonObject{{"candidate", candidate}, {"sdpMid", mid}}}});
        });
    });
    m_pc->onStateChange([this, gen](rtc::PeerConnection::State s) {
        post(gen, [this, s] {
            using S = rtc::PeerConnection::State;
            m_disconnectTimer.stop();
            if (s == S::Connected) {
                m_connectTimer.stop();
                m_reconnectAttempts = 0;
                setState(State::Connected);
                onConnected();
            } else if (s == S::Disconnected) {
                m_disconnectTimer.start(DisconnectedGraceMs);
            } else if (s == S::Failed) {
                reconnect();
            }
        });
    });
    if (m_inbound)
        m_pc->onTrack([this](std::shared_ptr<rtc::Track> track) { onRemoteTrack(std::move(track)); });

    setupPeerConnection(*m_pc);
    if (m_state != State::Reconnecting)
        setState(State::Connecting);
    armConnectTimer();
}

void RtcSession::sendOffer(bool isReconnect)
{
    if (!m_pc)
        return;
    m_reconnectPending = isReconnect;
    try {
        m_pc->setLocalDescription(rtc::Description::Type::Offer);
    } catch (const std::exception &e) {
        fail(QStringLiteral("StartOfferAnswer: %1").arg(QString::fromUtf8(e.what())));
    }
}

void RtcSession::onSignalingMessage(const QJsonObject &payload)
{
    if (m_state == State::Closed || m_state == State::Failed)
        return;
    const QString type = payload.value("type").toString();
    try {
        if (type == QLatin1String("offer"))
            onOffer(payload);
        else if (type == QLatin1String("answer"))
            onAnswer(payload);
        else if (type == QLatin1String("ice"))
            onCandidate(payload);
        else if (type == QLatin1String("reconnect") && !m_inbound) {
            qCInfo(lcRtc) << m_streamId << m_peerClid << "kReconnect recebido";
            setState(State::Reconnecting);
            createPeerConnection();
            sendOffer(true);
        }
    } catch (const std::exception &e) {
        qCWarning(lcRtc) << "OnStreamSignalingMessage" << type << "falhou:" << e.what();
    }
}

void RtcSession::onOffer(const QJsonObject &payload)
{
    if (!m_inbound)
        return;
    // Oferta de reconexão (ou renegociação): PeerConnection nova.
    if (!m_pc || payload.value("isReconnect").toBool() || m_pc->remoteDescription().has_value())
        createPeerConnection();
    const QString sdp = transformRemoteSdp(payload.value("sdp").toString());
    m_pc->setRemoteDescription(rtc::Description(sdp.toStdString(), rtc::Description::Type::Offer));
    const auto pending = std::exchange(m_pendingCandidates, {});
    for (const QJsonObject &c : pending)
        addCandidate(c);
    m_pc->setLocalDescription(rtc::Description::Type::Answer);
}

void RtcSession::onAnswer(const QJsonObject &payload)
{
    if (m_inbound || !m_pc)
        return;
    if (m_pc->signalingState() != rtc::PeerConnection::SignalingState::HaveLocalOffer) {
        qCWarning(lcRtc) << "kAnswer fora de hora";
        return;
    }
    m_pc->setRemoteDescription(rtc::Description(payload.value("sdp").toString().toStdString(), rtc::Description::Type::Answer));
    const auto pending = std::exchange(m_pendingCandidates, {});
    for (const QJsonObject &c : pending)
        addCandidate(c);
}

void RtcSession::onCandidate(const QJsonObject &payload)
{
    const QJsonValue c = payload.value("candidate");
    if (!c.isObject())
        return; // null = fim dos candidatos
    if (!m_pc || !m_pc->remoteDescription()) {
        m_pendingCandidates.append(c.toObject());
        return;
    }
    addCandidate(c.toObject());
}

void RtcSession::addCandidate(const QJsonObject &candidate)
{
    const QString cand = candidate.value("candidate").toString();
    if (cand.isEmpty())
        return;
    try {
        m_pc->addRemoteCandidate(rtc::Candidate(cand.toStdString(), candidate.value("sdpMid").toString().toStdString()));
    } catch (const std::exception &e) {
        qCDebug(lcRtc) << "candidato descartado" << e.what();
    }
}

void RtcSession::reconnect()
{
    if (m_state == State::Closed || m_state == State::Failed)
        return;
    if (++m_reconnectAttempts > MaxReconnectAttempts) {
        fail(QStringLiteral("número máximo de reconexões atingido"));
        return;
    }
    qCInfo(lcRtc) << m_streamId << m_peerClid << "Reconnect tentativa" << m_reconnectAttempts;
    setState(State::Reconnecting);
    armConnectTimer();
    if (m_inbound) {
        m_send({{"type", "reconnect"}});
    } else {
        createPeerConnection();
        sendOffer(true);
    }
}

void RtcSession::restart()
{
    if (m_state == State::New || m_state == State::Closed || m_state == State::Failed)
        return;
    if (m_state == State::Connected && m_pc && m_pc->state() == rtc::PeerConnection::State::Connected)
        return;
    m_disconnectTimer.stop();
    m_reconnectAttempts = 0;
    reconnect();
}

void RtcSession::fail(const QString &why)
{
    if (m_state == State::Closed || m_state == State::Failed)
        return;
    qCWarning(lcRtc) << m_streamId << m_peerClid << "falhou:" << why;
    setState(State::Failed);
    emit failed();
}

void RtcSession::close()
{
    if (m_state == State::Closed)
        return;
    m_connectTimer.stop();
    m_disconnectTimer.stop();
    ++m_generation;
    if (m_pc) {
        m_pc->resetCallbacks();
        m_pc->close();
        m_pc.reset();
    }
    setState(State::Closed);
}

void RtcSession::armConnectTimer() { m_connectTimer.start(ConnectTimeoutMs); }

void RtcSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace openspeak

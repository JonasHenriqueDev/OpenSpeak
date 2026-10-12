#include "MeshConnection.h"
#include "Protocol.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcMesh, "openspeak.mesh")

namespace openspeak {

namespace {

struct MeshError {
    QString code;
    QString message;
};
[[noreturn]] void fail(const QString &code, const QString &message = {}) { throw MeshError{code, message.isEmpty() ? code : message}; }

constexpr int MaintainMs = 3000;
constexpr int RetryMs = 10'000;
constexpr int JoinTimeoutMs = 10'000;
constexpr int MaxMessageBytes = 512 * 1024;

const struct { int cid; const char *name; const char *description; } Channels[] = {
    {1, "Lobby", "Canal padrão"},
    {2, "Jogos", "Partidas e transmissões de gameplay"},
    {3, "Trabalho", "Reuniões e compartilhamento de tela"},
    {4, "AFK", "Ausente"},
};

bool validChannel(int cid)
{
    for (const auto &c : Channels)
        if (c.cid == cid)
            return true;
    return false;
}

QString clean(const QJsonValue &v, int max)
{
    static const QRegularExpression control(QStringLiteral("[\\x00-\\x1f]"));
    return v.toString().remove(control).trimmed().left(max);
}

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

MeshConnection::MeshConnection(PeerDirectory *directory, QObject *parent)
    : ServerConnection(parent), m_dir(directory), m_server(QStringLiteral("OpenSpeak"), QWebSocketServer::NonSecureMode)
{
    m_server.setMaxPendingConnections(64);
    connect(&m_server, &QWebSocketServer::newConnection, this, [this] {
        while (QWebSocket *ws = m_server.nextPendingConnection()) {
            // Só aceita quem é da tailnet (o IP de origem é autenticado pelo WireGuard).
            if (!m_dir->find(ws->peerAddress())) {
                qCInfo(lcMesh) << "conexão recusada de" << ws->peerAddress().toString();
                ws->abort();
                ws->deleteLater();
                continue;
            }
            ws->setParent(this);
            ws->setMaxAllowedIncomingMessageSize(MaxMessageBytes);
            m_unidentified.insert(ws);
            connect(ws, &QWebSocket::textMessageReceived, this, [this, ws](const QString &t) { onMessage(ws, t); });
            connect(ws, &QWebSocket::disconnected, this, [this, ws] {
                m_unidentified.remove(ws);
                const QString key = m_socketKey.take(ws);
                if (!key.isEmpty() && m_peers.contains(key) && m_peers[key].socket == ws)
                    dropPeer(key, QStringLiteral("desconectou"));
                ws->deleteLater();
            });
        }
    });
    m_maintain.setInterval(MaintainMs);
    connect(&m_maintain, &QTimer::timeout, this, &MeshConnection::maintain);
    connect(m_dir, &PeerDirectory::changed, this, &MeshConnection::maintain);
}

MeshConnection::~MeshConnection() { close(); }

int MeshConnection::connectedPeers() const
{
    int n = 0;
    for (const Peer &p : m_peers)
        n += p.ready;
    return n;
}

void MeshConnection::start(const QString &nickname)
{
    m_nickname = nickname.trimmed().left(32);
    const PeerEntry self = m_dir->self();
    if (m_dir->status() != PeerDirectory::Status::Running || self.address.isNull()) {
        emit disconnected(QStringLiteral("tailnet_unavailable"));
        return;
    }
    if (!m_server.isListening() && !m_server.listen(self.address, self.port)) {
        qCWarning(lcMesh) << "não foi possível escutar em" << self.key() << m_server.errorString();
        emit disconnected(QStringLiteral("listen_failed"));
        return;
    }
    // Prefixo dos ids de stream: único por instalação/máquina.
    m_selfTag = QString::number(QRandomGenerator::global()->generate() % 0xFFFFFF, 36);
    m_running = true;
    m_maintain.start();
    qCInfo(lcMesh) << "malha ativa em" << self.key() << "como" << self.identityLabel();
    QMetaObject::invokeMethod(this, [this] {
        emit connected();
        maintain();
    }, Qt::QueuedConnection);
}

void MeshConnection::close()
{
    if (!m_running)
        return;
    m_running = false;
    m_maintain.stop();
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it)
        if (it->socket)
            it->socket->close();
    m_peers.clear();
    m_socketKey.clear();
    for (QWebSocket *ws : std::as_const(m_unidentified))
        ws->abort();
    m_unidentified.clear();
    m_server.close();
    m_own.clear();
    for (const JoinWait &w : std::as_const(m_joinWaits)) {
        w.timer->deleteLater();
        if (w.callback)
            w.callback({w.cmd, QStringLiteral("disconnected"), QString(), {}});
    }
    m_joinWaits.clear();
    emit disconnected(QStringLiteral("bye"));
}

// =================================================================== comandos

void MeshConnection::sendCommand(const QString &cmd, const QJsonObject &args, Callback callback)
{
    // Respostas sempre assíncronas, como na rede.
    QMetaObject::invokeMethod(this, [this, cmd, args, callback = std::move(callback)]() mutable {
        if (!m_running) {
            if (callback)
                callback({cmd, QStringLiteral("disconnected"), QStringLiteral("sem conexão"), {}});
            return;
        }
        if (cmd == QLatin1String("joinstreamrequest")) {
            cmdJoinRequest(args, std::move(callback));
            return;
        }
        CommandResult r{cmd, QStringLiteral("ok"), {}, {}};
        try {
            r.data = handle(cmd, args);
        } catch (const MeshError &e) {
            r.returnCode = e.code;
            r.error = e.message;
        }
        if (callback)
            callback(r);
    }, Qt::QueuedConnection);
}

QJsonObject MeshConnection::handle(const QString &cmd, const QJsonObject &a)
{
    if (cmd == QLatin1String("login")) return cmdLogin();
    if (cmd == QLatin1String("clientmove")) return cmdClientMove(a);
    if (cmd == QLatin1String("clientupdate")) {
        const QString nick = clean(a.value("nickname"), 32);
        if (nick.isEmpty())
            fail(rc::InvalidArgs);
        m_nickname = nick;
        emitLocal(QStringLiteral("notifyclientupdated"), {{"client", selfInfo()}});
        broadcastState();
        return {};
    }
    if (cmd == QLatin1String("setupstream")) return cmdSetupStream(a);
    if (cmd == QLatin1String("updatestream")) return cmdUpdateStream(a);
    if (cmd == QLatin1String("stopstream")) return cmdStopStream(a);
    if (cmd == QLatin1String("streamthumbnail")) return cmdThumbnail(a);
    if (cmd == QLatin1String("respondjoinstreamrequest")) return cmdRespondJoin(a);
    if (cmd == QLatin1String("removeclientfromstream")) return cmdRemoveClient(a);
    if (cmd == QLatin1String("streamunban")) return cmdUnban(a);
    if (cmd == QLatin1String("streamsignaling")) return cmdSignaling(a);
    if (cmd == QLatin1String("requeststreaminfo")) {
        if (OwnStream *s = own(a.value("streamIdentifier").toString()))
            return {{"stream", publicOwnStream(*s)}};
        if (Peer *p = ownerOf(a.value("streamIdentifier").toString()))
            return {{"stream", remoteStreamFor(*p, p->streams.value(a.value("streamIdentifier").toString()))}};
        fail(rc::StreamNotFound);
    }
    fail(rc::UnknownCommand);
}

QJsonObject MeshConnection::cmdLogin()
{
    QJsonArray channels, clients, streams;
    for (const auto &c : Channels)
        channels.append(QJsonObject{{"cid", c.cid}, {"order", c.cid}, {"name", QString::fromUtf8(c.name)},
                                    {"description", QString::fromUtf8(c.description)}});
    clients.append(selfInfo());
    for (const Peer &p : m_peers) {
        if (!p.ready)
            continue;
        clients.append(clientInfo(p));
        for (const QJsonObject &s : p.streams)
            streams.append(remoteStreamFor(p, s));
    }
    for (const OwnStream &s : m_own)
        streams.append(publicOwnStream(s));
    const QString network = m_dir->networkName();
    return {
        {"clid", 1},
        {"server", QJsonObject{{"name", network.isEmpty() ? QStringLiteral("Tailnet") : QStringLiteral("Tailnet · ") + network},
                               {"capabilityExtensions", QJsonArray{CapStreamP2P}}}},
        {"iceServers", QJsonArray{}}, // dentro da tailnet os candidatos locais bastam
        {"channels", channels},
        {"clients", clients},
        {"streams", streams},
        {"mesh", true},
    };
}

QJsonObject MeshConnection::cmdClientMove(const QJsonObject &a)
{
    const int cid = a.value("cid").toInt();
    if (!validChannel(cid))
        fail(rc::ChannelNotFound);
    if (cid == m_cid)
        return {};
    // Streams pertencem à sala: sair encerra os próprios.
    for (const QString &id : m_own.keys()) {
        m_own.remove(id);
        emitLocal(QStringLiteral("notifystreamstopped"), {{"streamIdentifier", id}, {"reason", int(LeaveReason::StreamStopped)}});
    }
    m_cid = cid;
    emitLocal(QStringLiteral("notifyclientmoved"), {{"clid", 1}, {"cid", cid}});
    broadcastState(); // os donos dos streams que eu assistia me removem ao ver a mudança
    return {};
}

void MeshConnection::applyConfig(OwnStream &s, const QJsonObject &a)
{
    auto clampInt = [&](const char *k, int lo, int hi, int &field) {
        if (a.contains(QLatin1String(k)) && a.value(QLatin1String(k)).isDouble())
            field = std::clamp(a.value(QLatin1String(k)).toInt(), lo, hi);
    };
    if (a.contains("name")) {
        const QString n = clean(a.value("name"), 64);
        if (!n.isEmpty())
            s.name = n;
    }
    clampInt("type", 0, 4, s.type);
    if (a.contains("access")) {
        const int acc = a.value("access").toInt();
        if (acc >= 1 && acc <= 3)
            s.access = acc;
    }
    clampInt("bitrate", 100'000, 50'000'000, s.bitrate);
    clampInt("viewerLimit", 0, 1000, s.viewerLimit);
    if (a.contains("audio"))
        s.audio = a.value("audio").toBool();
    if (a.contains("paused"))
        s.paused = a.value("paused").toBool();
    if (a.value("quality").isObject()) {
        const QJsonObject q = a.value("quality").toObject();
        s.width = std::clamp(q.value("width").toInt(s.width), -1, 7680);
        s.height = std::clamp(q.value("height").toInt(s.height), -1, 4320);
        s.fps = std::clamp(q.value("fps").toInt(s.fps), 1, 144);
    }
    if (s.access != int(StreamAccess::Public))
        s.thumbnail.clear();
}

QJsonObject MeshConnection::cmdSetupStream(const QJsonObject &a)
{
    if (a.contains("mode") && a.value("mode").toInt() != int(StreamMode::P2P))
        fail(rc::ModeNotSupported);
    if (m_own.size() >= MaxStreamsPerClient)
        fail(rc::StreamLimit);
    OwnStream s;
    s.id = m_selfTag + QLatin1Char('-') + QString::number(m_nextStream++);
    s.cid = m_cid;
    s.created = now();
    s.name = QStringLiteral("Stream de %1").arg(m_nickname);
    applyConfig(s, a);
    m_own.insert(s.id, s);
    emitLocal(QStringLiteral("notifystreamstarted"), {{"stream", publicOwnStream(s)}});
    broadcastState();
    return {{"streamIdentifier", s.id}};
}

QJsonObject MeshConnection::cmdUpdateStream(const QJsonObject &a)
{
    OwnStream *s = own(a.value("streamIdentifier").toString());
    if (!s)
        fail(rc::PermissionDenied);
    applyConfig(*s, a);
    emitLocal(QStringLiteral("notifystreamupdated"), {{"stream", publicOwnStream(*s)}});
    broadcastState();
    return {};
}

QJsonObject MeshConnection::cmdStopStream(const QJsonObject &a)
{
    const QString id = a.value("streamIdentifier").toString();
    if (!m_own.remove(id))
        fail(rc::StreamNotFound);
    emitLocal(QStringLiteral("notifystreamstopped"), {{"streamIdentifier", id}, {"reason", int(LeaveReason::StreamStopped)}});
    broadcastState();
    return {};
}

QJsonObject MeshConnection::cmdThumbnail(const QJsonObject &a)
{
    OwnStream *s = own(a.value("streamIdentifier").toString());
    if (!s)
        fail(rc::PermissionDenied);
    const QString thumb = a.value("thumbnail").toString();
    if (!thumb.startsWith(QLatin1String("data:image/")))
        fail(rc::InvalidArgs);
    if (thumb.size() > MaxThumbnailBytes)
        fail(rc::PayloadTooLarge);
    if (s->access != int(StreamAccess::Public))
        return {{"published", false}};
    s->thumbnail = thumb;
    for (Peer &p : m_peers)
        if (p.ready && p.cid == s->cid)
            sendTo(p, {{"t", "thumb"}, {"stream", s->id}, {"data", thumb}});
    return {{"published", true}};
}

void MeshConnection::cmdJoinRequest(const QJsonObject &a, Callback callback)
{
    const QString id = a.value("streamIdentifier").toString();
    Peer *owner = ownerOf(id);
    auto reply = [&](const QString &code) {
        if (callback)
            callback({QStringLiteral("joinstreamrequest"), code, code, {}});
    };
    if (!owner)
        return reply(rc::StreamNotFound);
    if (owner->streams.value(id).value("cid").toInt() != m_cid)
        return reply(rc::NotInChannel);
    const int req = m_nextJoin++;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, req] {
        JoinWait w = m_joinWaits.take(req);
        if (w.timer)
            w.timer->deleteLater();
        if (w.callback)
            w.callback({w.cmd, QStringLiteral("timeout"), QStringLiteral("o transmissor não respondeu"), {}});
    });
    timer->start(JoinTimeoutMs);
    m_joinWaits.insert(req, JoinWait{std::move(callback), QStringLiteral("joinstreamrequest"), timer});
    sendTo(*owner, {{"t", "join"}, {"stream", id}, {"codecs", a.value("codecs").toArray()}, {"req", req}});
}

QJsonObject MeshConnection::cmdRespondJoin(const QJsonObject &a)
{
    OwnStream *s = own(a.value("streamIdentifier").toString());
    if (!s)
        fail(rc::PermissionDenied);
    const int clid = a.value("clid").toInt();
    if (!s->pending.contains(clid))
        fail(rc::NoPendingRequest);
    Peer *viewer = peerByClid(clid);
    if (!viewer || !viewer->ready || viewer->cid != s->cid) {
        s->pending.remove(clid);
        fail(rc::ClientNotFound);
    }
    if (a.value("accept").toBool()) {
        if (s->viewerLimit > 0 && s->viewers.size() >= s->viewerLimit)
            fail(rc::ViewerLimit);
        const QJsonArray codecs = s->pending.take(clid);
        s->viewers.append(clid);
        sendTo(*viewer, {{"t", "respond"}, {"stream", s->id}, {"accepted", true}});
        emitLocal(QStringLiteral("notifystreamclientjoined"), {{"streamIdentifier", s->id}, {"clid", clid}, {"codecs", codecs}});
        notifyAttendees(*s);
    } else {
        s->pending.remove(clid);
        const bool ban = a.value("ban").toBool();
        if (ban && !s->banned.contains(viewer->entry.identity())) {
            s->banned.insert(viewer->entry.identity());
            emitLocal(QStringLiteral("notifystreambanadded"), {{"streamIdentifier", s->id}, {"uid", viewer->entry.identity()}});
        }
        sendTo(*viewer, {{"t", "respond"}, {"stream", s->id}, {"accepted", false},
                         {"reason", int(ban ? LeaveReason::Banned : LeaveReason::Denied)}});
    }
    return {};
}

QJsonObject MeshConnection::cmdRemoveClient(const QJsonObject &a)
{
    const QString id = a.value("streamIdentifier").toString();
    if (OwnStream *s = own(id)) {
        const int target = a.value("clid").toInt();
        int reason = a.value("reason").toInt(int(LeaveReason::Kicked));
        if (reason != int(LeaveReason::Banned) && reason != int(LeaveReason::Failed))
            reason = int(LeaveReason::Kicked);
        if (reason == int(LeaveReason::Banned)) {
            if (Peer *p = peerByClid(target); p && !s->banned.contains(p->entry.identity())) {
                s->banned.insert(p->entry.identity());
                emitLocal(QStringLiteral("notifystreambanadded"), {{"streamIdentifier", id}, {"uid", p->entry.identity()}});
            }
        }
        removeViewer(*s, target, reason, true);
        return {};
    }
    // Espectador saindo (ou cancelando o pedido) de um stream de outro par.
    Peer *owner = ownerOf(id);
    if (!owner)
        fail(rc::StreamNotFound);
    sendTo(*owner, {{"t", "leave"}, {"stream", id}});
    emitLocal(QStringLiteral("notifystreamclientleft"),
              {{"streamIdentifier", id}, {"clid", 1}, {"reason", int(LeaveReason::Left)}, {"wasPending", false}});
    return {};
}

QJsonObject MeshConnection::cmdUnban(const QJsonObject &a)
{
    OwnStream *s = own(a.value("streamIdentifier").toString());
    if (!s)
        fail(rc::PermissionDenied);
    const QString uid = a.value("uid").toString();
    if (s->banned.remove(uid))
        emitLocal(QStringLiteral("notifystreambanremoved"), {{"streamIdentifier", s->id}, {"uid", uid}});
    return {};
}

QJsonObject MeshConnection::cmdSignaling(const QJsonObject &a)
{
    const QString id = a.value("streamIdentifier").toString();
    const QJsonObject payload = a.value("payload").toObject();
    if (!payload.value("type").isString())
        fail(rc::InvalidArgs);
    if (QJsonDocument(payload).toJson(QJsonDocument::Compact).size() > MaxSignalingBytes)
        fail(rc::PayloadTooLarge);
    Peer *target = peerByClid(a.value("clid").toInt());
    if (!target || !target->ready)
        fail(rc::ClientNotFound);
    const OwnStream *s = own(id);
    const bool ownerToViewer = s && s->viewers.contains(target->clid);
    const bool viewerToOwner = !s && target->streams.contains(id);
    if (!ownerToViewer && !viewerToOwner)
        fail(rc::PermissionDenied);
    sendTo(*target, {{"t", "signal"}, {"stream", id}, {"payload", payload}});
    return {};
}

// ===================================================================== malha

void MeshConnection::maintain()
{
    if (!m_running)
        return;
    const PeerEntry self = m_dir->self();
    QSet<QString> online;
    for (const PeerEntry &e : m_dir->peers()) {
        if (!e.online)
            continue;
        online.insert(e.key());
        auto it = m_peers.find(e.key());
        if (it != m_peers.end()) {
            it->entry = e; // identidade/hostname podem mudar
            if (it->socket)
                continue;
        }
        // Evita conexões duplicadas: só o lado de chave menor inicia.
        if (self.key() < e.key() && (it == m_peers.end() || now() >= it->retryAt))
            connectTo(e);
    }
    // Pares que saíram da tailnet.
    for (const QString &key : m_peers.keys())
        if (!online.contains(key))
            dropPeer(key, QStringLiteral("offline na tailnet"));
}

void MeshConnection::connectTo(const PeerEntry &entry)
{
    Peer &peer = m_peers[entry.key()];
    peer.entry = entry;
    peer.retryAt = now() + RetryMs;
    auto *ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    ws->setMaxAllowedIncomingMessageSize(MaxMessageBytes);
    peer.socket = ws;
    const QString key = entry.key();
    connect(ws, &QWebSocket::connected, this, [this, ws, key] {
        attachSocket(ws, key);
        ws->sendTextMessage(QString::fromUtf8(QJsonDocument(stateMessage("hello")).toJson(QJsonDocument::Compact)));
    });
    connect(ws, &QWebSocket::textMessageReceived, this, [this, ws](const QString &t) { onMessage(ws, t); });
    connect(ws, &QWebSocket::disconnected, this, [this, ws, key] {
        m_socketKey.remove(ws);
        auto it = m_peers.find(key);
        if (it != m_peers.end() && it->socket == ws) {
            if (it->ready)
                dropPeer(key, QStringLiteral("desconectou"));
            else
                it->socket = nullptr; // sem OpenSpeak nesse par; tenta de novo depois
        }
        ws->deleteLater();
    });
    ws->open(QUrl(QStringLiteral("ws://%1:%2/mesh").arg(entry.address.toString()).arg(entry.port)));
}

void MeshConnection::attachSocket(QWebSocket *ws, const QString &key)
{
    m_socketKey.insert(ws, key);
}

void MeshConnection::onMessage(QWebSocket *ws, const QString &text)
{
    const QJsonObject m = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString t = m.value("t").toString();

    if (m_unidentified.contains(ws)) {
        // Primeiro contato de entrada: precisa ser hello de um par conhecido da tailnet.
        if (t != QLatin1String("hello") || m.value("proto").toInt() != ProtocolVersion) {
            ws->close();
            return;
        }
        const auto entry = m_dir->find(ws->peerAddress(), quint16(m.value("port").toInt()));
        if (!entry) {
            ws->close();
            return;
        }
        m_unidentified.remove(ws);
        Peer &peer = m_peers[entry->key()];
        if (peer.socket && peer.socket != ws && peer.ready) {
            ws->close(); // conexão duplicada
            return;
        }
        if (peer.socket && peer.socket != ws)
            peer.socket->abort();
        peer.entry = *entry;
        peer.socket = ws;
        m_socketKey.insert(ws, entry->key());
        ws->sendTextMessage(QString::fromUtf8(QJsonDocument(stateMessage("hello")).toJson(QJsonDocument::Compact)));
    }

    const QString key = m_socketKey.value(ws);
    auto it = m_peers.find(key);
    if (key.isEmpty() || it == m_peers.end() || it->socket != ws)
        return;
    onPeerMessage(*it, m);
}

void MeshConnection::onPeerMessage(Peer &peer, const QJsonObject &m)
{
    const QString t = m.value("t").toString();
    if (t == QLatin1String("hello") || t == QLatin1String("state")) {
        applyState(peer, m);
        return;
    }
    if (!peer.ready)
        return;
    const QString id = m.value("stream").toString();

    if (t == QLatin1String("join")) {
        // Sou o dono: valido e enfileiro o pedido.
        auto answer = [&](const QString &err) {
            sendTo(peer, {{"t", "joinreply"}, {"req", m.value("req")}, {"error", err}});
        };
        OwnStream *s = own(id);
        if (!s) return answer(rc::StreamNotFound);
        if (peer.cid != s->cid) return answer(rc::NotInChannel);
        if (s->banned.contains(peer.entry.identity())) return answer(rc::Banned);
        if (s->viewers.contains(peer.clid)) return answer(rc::AlreadyJoined);
        if (s->viewerLimit > 0 && s->viewers.size() >= s->viewerLimit) return answer(rc::ViewerLimit);
        QJsonArray codecs;
        for (const QJsonValue &c : m.value("codecs").toArray())
            if (c.isString() && codecs.size() < 16)
                codecs.append(c);
        s->pending.insert(peer.clid, codecs);
        answer(QString());
        emitLocal(QStringLiteral("notifyjoinstreamrequest"),
                  {{"streamIdentifier", id}, {"clid", peer.clid}, {"nickname", peer.nickname},
                   {"uid", peer.entry.identity()}, {"identity", peer.entry.identityLabel()}, {"codecs", codecs}});
    } else if (t == QLatin1String("joinreply")) {
        JoinWait w = m_joinWaits.take(m.value("req").toInt());
        if (w.timer)
            w.timer->deleteLater();
        const QString err = m.value("error").toString();
        if (w.callback)
            w.callback({w.cmd, err.isEmpty() ? rc::Ok : err, err, err.isEmpty() ? QJsonObject{{"pending", true}} : QJsonObject{}});
    } else if (t == QLatin1String("respond")) {
        if (!peer.streams.contains(id))
            return;
        QJsonObject args{{"streamIdentifier", id}, {"accepted", m.value("accepted").toBool()}, {"ownerClid", peer.clid}};
        if (m.contains("reason"))
            args["reason"] = m.value("reason").toInt();
        emitLocal(QStringLiteral("notifyrespondjoinstreamrequest"), args);
    } else if (t == QLatin1String("leave")) {
        if (OwnStream *s = own(id))
            removeViewer(*s, peer.clid, int(LeaveReason::Left), false);
    } else if (t == QLatin1String("removed")) {
        if (peer.streams.contains(id))
            emitLocal(QStringLiteral("notifystreamclientleft"),
                      {{"streamIdentifier", id}, {"clid", 1}, {"reason", m.value("reason").toInt()},
                       {"wasPending", m.value("wasPending").toBool()}});
    } else if (t == QLatin1String("signal")) {
        const OwnStream *s = own(id);
        const bool fromViewer = s && s->viewers.contains(peer.clid);
        const bool fromOwner = !s && peer.streams.contains(id);
        if (fromViewer || fromOwner)
            emitLocal(QStringLiteral("notifystreamsignaling"),
                      {{"streamIdentifier", id}, {"clid", peer.clid}, {"payload", m.value("payload").toObject()}});
    } else if (t == QLatin1String("thumb")) {
        if (!peer.streams.contains(id))
            return;
        const QString data = m.value("data").toString();
        if (!data.startsWith(QLatin1String("data:image/")) || data.size() > MaxThumbnailBytes)
            return;
        peer.streams[id]["thumbnail"] = data;
        emitLocal(QStringLiteral("notifystreamthumbnail"), {{"streamIdentifier", id}, {"thumbnail", data}});
    }
}

void MeshConnection::applyState(Peer &peer, const QJsonObject &m)
{
    const bool first = !peer.ready;
    if (first) {
        peer.ready = true;
        peer.clid = clidFor(peer.entry.key());
    }
    const QString nickname = clean(m.value("nickname"), 32).isEmpty() ? peer.entry.hostName : clean(m.value("nickname"), 32);
    const int cid = validChannel(m.value("cid").toInt()) ? m.value("cid").toInt() : 1;
    const bool nickChanged = nickname != peer.nickname;
    const bool moved = !first && cid != peer.cid;
    peer.nickname = nickname;
    peer.cid = cid;

    if (first) {
        qCInfo(lcMesh).noquote() << "par conectado:" << peer.nickname << "(" + peer.entry.identityLabel() + ")";
        emitLocal(QStringLiteral("notifycliententerview"), {{"client", clientInfo(peer)}});
    } else if (nickChanged) {
        emitLocal(QStringLiteral("notifyclientupdated"), {{"client", clientInfo(peer)}});
    }
    if (moved) {
        emitLocal(QStringLiteral("notifyclientmoved"), {{"clid", peer.clid}, {"cid", cid}});
        // Saiu da sala: deixa de assistir aos meus streams.
        for (OwnStream &s : m_own)
            if (s.cid != cid && (s.viewers.contains(peer.clid) || s.pending.contains(peer.clid)))
                removeViewer(s, peer.clid, int(LeaveReason::Left), true);
    }

    // Diferença dos streams anunciados.
    QMap<QString, QJsonObject> incoming;
    for (const QJsonValue &v : m.value("streams").toArray()) {
        QJsonObject s = v.toObject();
        const QString id = s.value("streamIdentifier").toString();
        if (id.isEmpty() || incoming.size() >= MaxStreamsPerClient)
            continue;
        if (s.value("thumbnail").isNull() && peer.streams.contains(id))
            s["thumbnail"] = peer.streams[id].value("thumbnail"); // miniatura vem à parte
        incoming.insert(id, s);
    }
    for (const QString &id : peer.streams.keys())
        if (!incoming.contains(id))
            emitLocal(QStringLiteral("notifystreamstopped"), {{"streamIdentifier", id}, {"reason", int(LeaveReason::StreamStopped)}});
    for (auto it = incoming.begin(); it != incoming.end(); ++it) {
        const bool known = peer.streams.contains(it.key());
        if (!known)
            emitLocal(QStringLiteral("notifystreamstarted"), {{"stream", remoteStreamFor(peer, it.value())}});
        else if (peer.streams.value(it.key()) != it.value())
            emitLocal(QStringLiteral("notifystreamupdated"), {{"stream", remoteStreamFor(peer, it.value())}});
    }
    peer.streams = incoming;
}

void MeshConnection::dropPeer(const QString &key, const QString &why)
{
    auto it = m_peers.find(key);
    if (it == m_peers.end())
        return;
    Peer peer = *it;
    m_peers.erase(it);
    if (peer.socket) {
        m_socketKey.remove(peer.socket);
        peer.socket->abort();
    }
    if (!peer.ready)
        return;
    qCInfo(lcMesh).noquote() << "par saiu:" << peer.nickname << "-" << why;
    for (const QString &id : peer.streams.keys())
        emitLocal(QStringLiteral("notifystreamstopped"), {{"streamIdentifier", id}, {"reason", int(LeaveReason::StreamStopped)}});
    for (OwnStream &s : m_own)
        if (s.viewers.contains(peer.clid) || s.pending.contains(peer.clid))
            removeViewer(s, peer.clid, int(LeaveReason::Left), false);
    emitLocal(QStringLiteral("notifyclientleftview"), {{"clid", peer.clid}});
}

void MeshConnection::sendTo(Peer &peer, const QJsonObject &m)
{
    if (peer.socket && peer.socket->state() == QAbstractSocket::ConnectedState)
        peer.socket->sendTextMessage(QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
}

QJsonObject MeshConnection::stateMessage(const char *type) const
{
    QJsonArray streams;
    for (const OwnStream &s : m_own) {
        QJsonObject p = publicOwnStream(s);
        p["thumbnail"] = QJsonValue::Null; // enviada separadamente
        streams.append(p);
    }
    return {{"t", QLatin1String(type)}, {"proto", ProtocolVersion}, {"port", int(m_dir->self().port)},
            {"nickname", m_nickname}, {"cid", m_cid}, {"streams", streams}};
}

void MeshConnection::broadcastState()
{
    const QJsonObject msg = stateMessage("state");
    for (Peer &p : m_peers)
        if (p.ready)
            sendTo(p, msg);
}

// ============================================================ streams próprios

MeshConnection::OwnStream *MeshConnection::own(const QString &id)
{
    auto it = m_own.find(id);
    return it != m_own.end() ? &it.value() : nullptr;
}

void MeshConnection::removeViewer(OwnStream &s, int clid, int reason, bool notifyPeer)
{
    const bool wasViewer = s.viewers.removeAll(clid) > 0;
    const bool wasPending = s.pending.remove(clid) > 0;
    if (!wasViewer && !wasPending)
        return;
    if (notifyPeer)
        if (Peer *p = peerByClid(clid))
            sendTo(*p, {{"t", "removed"}, {"stream", s.id}, {"reason", reason}, {"wasPending", wasPending}});
    emitLocal(QStringLiteral("notifystreamclientleft"),
              {{"streamIdentifier", s.id}, {"clid", clid}, {"reason", reason}, {"wasPending", wasPending}});
    if (wasViewer)
        notifyAttendees(s);
}

void MeshConnection::notifyAttendees(const OwnStream &s)
{
    QJsonArray viewers;
    for (int v : s.viewers)
        viewers.append(v);
    emitLocal(QStringLiteral("notifystreamattendees"), {{"streamIdentifier", s.id}, {"viewers", viewers}});
    emitLocal(QStringLiteral("notifystreamupdated"), {{"stream", publicOwnStream(s)}});
    broadcastState(); // contagem de espectadores é pública
}

QJsonObject MeshConnection::publicOwnStream(const OwnStream &s) const
{
    return {
        {"streamIdentifier", s.id}, {"ownerClid", 1}, {"cid", s.cid}, {"name", s.name}, {"type", s.type},
        {"access", s.access}, {"mode", s.mode}, {"bitrate", s.bitrate}, {"viewerLimit", s.viewerLimit},
        {"audio", s.audio}, {"paused", s.paused}, {"created", double(s.created)},
        {"quality", QJsonObject{{"width", s.width}, {"height", s.height}, {"fps", s.fps}}},
        {"viewerCount", int(s.viewers.size())},
        {"thumbnail", s.access == int(StreamAccess::Public) && !s.thumbnail.isEmpty() ? QJsonValue(s.thumbnail) : QJsonValue()},
    };
}

QJsonObject MeshConnection::remoteStreamFor(const Peer &peer, QJsonObject info) const
{
    info["ownerClid"] = peer.clid; // ids de cliente são locais a cada nó
    return info;
}

// ================================================================= auxiliares

MeshConnection::Peer *MeshConnection::peerByClid(int clid)
{
    for (Peer &p : m_peers)
        if (p.ready && p.clid == clid)
            return &p;
    return nullptr;
}

MeshConnection::Peer *MeshConnection::ownerOf(const QString &streamId, QString *outKey)
{
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (it->ready && it->streams.contains(streamId)) {
            if (outKey)
                *outKey = it.key();
            return &it.value();
        }
    }
    return nullptr;
}

int MeshConnection::clidFor(const QString &key)
{
    auto it = m_clidByKey.find(key);
    if (it == m_clidByKey.end())
        it = m_clidByKey.insert(key, m_nextClid++);
    return it.value();
}

QJsonObject MeshConnection::clientInfo(const Peer &peer) const
{
    return {{"clid", peer.clid}, {"nickname", peer.nickname}, {"uid", peer.entry.identity()}, {"cid", peer.cid},
            {"identity", peer.entry.identityLabel()}, {"inputMuted", false}, {"outputMuted", false}};
}

QJsonObject MeshConnection::selfInfo() const
{
    const PeerEntry self = m_dir->self();
    return {{"clid", 1}, {"nickname", m_nickname}, {"uid", self.identity()}, {"cid", m_cid},
            {"identity", self.identityLabel()}, {"inputMuted", false}, {"outputMuted", false}};
}

void MeshConnection::emitLocal(const QString &cmd, const QJsonObject &args)
{
    // Assíncrono e em ordem, como notificações vindas da rede.
    QMetaObject::invokeMethod(this, [this, cmd, args] {
        if (m_running)
            emit notification(cmd, args);
    }, Qt::QueuedConnection);
}

} // namespace openspeak

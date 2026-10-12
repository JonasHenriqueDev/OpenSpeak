#include "ConnectionHandler.h"

#include <QUrl>

#include <algorithm>

namespace openspeak {

StreamInfo StreamInfo::fromJson(const QJsonObject &o)
{
    StreamInfo s;
    s.id = o.value("streamIdentifier").toString();
    s.ownerClid = o.value("ownerClid").toInt();
    s.cid = o.value("cid").toInt();
    s.name = o.value("name").toString();
    s.type = StreamType(o.value("type").toInt());
    s.access = StreamAccess(o.value("access").toInt());
    s.bitrate = o.value("bitrate").toInt();
    s.viewerLimit = o.value("viewerLimit").toInt();
    s.audio = o.value("audio").toBool();
    const QJsonObject q = o.value("quality").toObject();
    s.quality = {q.value("width").toInt(1280), q.value("height").toInt(720), q.value("fps").toInt(30)};
    s.paused = o.value("paused").toBool();
    s.created = qint64(o.value("created").toDouble());
    s.viewerCount = o.value("viewerCount").toInt();
    s.thumbnail = o.value("thumbnail").toString();
    return s;
}

QVariantMap streamToVariant(const StreamInfo &s)
{
    return {
        {"streamIdentifier", s.id}, {"ownerClid", s.ownerClid}, {"cid", s.cid}, {"name", s.name},
        {"type", int(s.type)}, {"access", int(s.access)}, {"bitrate", s.bitrate}, {"viewerLimit", s.viewerLimit},
        {"audio", s.audio}, {"paused", s.paused}, {"viewerCount", s.viewerCount}, {"thumbnail", s.thumbnail},
        {"width", s.quality.width}, {"height", s.quality.height}, {"fps", s.quality.fps},
    };
}

static ClientInfo clientFromJson(const QJsonObject &c)
{
    return {c.value("clid").toInt(), c.value("cid").toInt(), c.value("nickname").toString(), c.value("uid").toString(),
            c.value("identity").toString()};
}

ConnectionHandler::ConnectionHandler(QObject *parent) : QObject(parent)
{
    m_ws = new WebSocketConnection(this);
    connect(&m_connection, &ServerConnection::connected, this, &ConnectionHandler::login);
    connect(&m_connection, &ServerConnection::disconnected, this, [this](const QString &reason) {
        const bool was = m_loggedIn;
        reset();
        if (was)
            emit disconnected(reason);
        else
            emit connectFailed(reason.isEmpty() ? tr("não foi possível conectar ao servidor") : reason);
    });
    connect(&m_connection, &ServerConnection::notification, this, &ConnectionHandler::onNotification);
}

void ConnectionHandler::setDirectory(PeerDirectory *directory)
{
    m_directory = directory;
    m_mesh = new MeshConnection(directory, this);
    connect(directory, &PeerDirectory::changed, this, &ConnectionHandler::tailnetChanged);
    directory->start();
}

QString ConnectionHandler::tailnetStatus() const
{
    if (!m_directory)
        return QStringLiteral("unavailable");
    switch (m_directory->status()) {
    case PeerDirectory::Status::Running: return QStringLiteral("running");
    case PeerDirectory::Status::NotInstalled: return QStringLiteral("not_installed");
    case PeerDirectory::Status::Stopped: return QStringLiteral("stopped");
    case PeerDirectory::Status::NeedsLogin: return QStringLiteral("needs_login");
    case PeerDirectory::Status::Unknown: break;
    }
    return QStringLiteral("unknown");
}

int ConnectionHandler::tailnetOnline() const
{
    int n = 0;
    if (m_directory)
        for (const PeerEntry &p : m_directory->peers())
            n += p.online;
    return n;
}

void ConnectionHandler::connectTailnet(const QString &nickname)
{
    m_cloudMode = false;
    if (!m_mesh) {
        emit connectFailed(QStringLiteral("tailnet_unavailable"));
        return;
    }
    m_pendingNickname = nickname;
    m_pendingUid = m_directory->self().identity();
    m_connection.setBackend(m_mesh);
    emit connectionChanged();
    m_mesh->start(nickname);
}

void ConnectionHandler::connectTo(const QString &url, const QString &nickname, const QString &uid)
{
    m_cloudMode = false;
    m_ws->setCloudMode(false);
    m_pendingNickname = nickname;
    m_pendingUid = uid;
    m_connection.setBackend(m_ws);
    emit connectionChanged();
    m_ws->open(QUrl(url));
}

void ConnectionHandler::connectCloud(const QString &nickname, const QString &uid)
{
    if (!m_directory || m_directory->status() != PeerDirectory::Status::Running || m_directory->self().address.isNull()) {
        emit connectFailed(QStringLiteral("Conecte o Tailscale antes de entrar na sala web."));
        return;
    }
    m_cloudMode = true;
    m_reconnecting = false;
    m_pendingNickname = nickname;
    m_pendingUid = uid;
    m_ws->setCloudMode(true);
    m_connection.setBackend(m_ws);
    emit connectionChanged();
    m_ws->open(QUrl(QStringLiteral("wss://openspeak-dun.vercel.app/ws")));
}

void ConnectionHandler::login()
{
    m_connection.sendCommand(QStringLiteral("login"), {{"nickname", m_pendingNickname}, {"uid", m_pendingUid}},
                             [this](const CommandResult &r) {
        if (!r.ok()) {
            emit connectFailed(r.error.isEmpty() ? r.returnCode : r.error);
            m_connection.close();
            return;
        }
        applyView(r.data);
    });
}

void ConnectionHandler::applyView(const QJsonObject &d)
{
        const bool wasLoggedIn = m_loggedIn;
        const bool changedIdentity = d.value("restarted").toBool() || (m_loggedIn && m_ownClid != d.value("clid").toInt());
        const auto previous = m_streams;
        m_ownClid = d.value("clid").toInt();
        const QJsonObject server = d.value("server").toObject();
        m_serverName = server.value("name").toString();
        m_capabilities.clear();
        for (const QJsonValue &v : server.value("capabilityExtensions").toArray())
            m_capabilities.append(v.toString());
        m_iceServers = d.value("iceServers").toArray();
        m_channels.clear();
        for (const QJsonValue &v : d.value("channels").toArray()) {
            const QJsonObject c = v.toObject();
            m_channels.insert(c.value("cid").toInt(), {c.value("cid").toInt(), c.value("order").toInt(),
                                                       c.value("name").toString(), c.value("description").toString()});
        }
        m_clients.clear();
        for (const QJsonValue &v : d.value("clients").toArray()) {
            const ClientInfo c = clientFromJson(v.toObject());
            m_clients.insert(c.clid, c);
        }
        m_streams.clear();
        for (const QJsonValue &v : d.value("streams").toArray()) {
            const StreamInfo s = StreamInfo::fromJson(v.toObject());
            m_streams.insert(s.id, s);
        }
        for (auto it = previous.begin(); it != previous.end(); ++it)
            if (changedIdentity || !m_streams.contains(it.key())) emit streamRemoved(it.key(), int(LeaveReason::Left));
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it)
            if (!previous.contains(it.key()) && m_loggedIn) emit streamAdded(it.key());
        m_loggedIn = true;
        if (!wasLoggedIn) emit connectionChanged();
        emit changed();
}

void ConnectionHandler::disconnectFromServer() { m_connection.close(); }

void ConnectionHandler::moveTo(int cid)
{
    m_connection.sendCommand(QStringLiteral("clientmove"), {{"cid", cid}});
}

QString ConnectionHandler::clientName(int clid) const
{
    const auto it = m_clients.find(clid);
    return it != m_clients.end() ? it->nickname : QStringLiteral("#%1").arg(clid);
}

QString ConnectionHandler::clientIdentity(int clid) const
{
    const auto it = m_clients.find(clid);
    return it != m_clients.end() ? it->identity : QString();
}

int ConnectionHandler::ownChannel() const
{
    const auto it = m_clients.find(m_ownClid);
    return it != m_clients.end() ? it->cid : 0;
}

const StreamInfo *ConnectionHandler::stream(const QString &id) const
{
    const auto it = m_streams.find(id);
    return it != m_streams.end() ? &it.value() : nullptr;
}

QList<StreamInfo> ConnectionHandler::streamsInChannel(int cid) const
{
    QList<StreamInfo> out;
    for (const StreamInfo &s : m_streams)
        if (s.cid == cid)
            out.append(s);
    std::sort(out.begin(), out.end(), [](const StreamInfo &a, const StreamInfo &b) { return a.created < b.created; });
    return out;
}

const ClientInfo *ConnectionHandler::client(int clid) const
{
    const auto it = m_clients.find(clid);
    return it != m_clients.end() ? &it.value() : nullptr;
}

QVariantList ConnectionHandler::tree() const
{
    QList<ChannelInfo> channels = m_channels.values();
    std::sort(channels.begin(), channels.end(), [](const auto &a, const auto &b) { return a.order < b.order; });
    const int own = ownChannel();
    QVariantList out;
    for (const ChannelInfo &ch : channels) {
        QList<ClientInfo> clients;
        for (const ClientInfo &c : m_clients)
            if (c.cid == ch.cid)
                clients.append(c);
        std::sort(clients.begin(), clients.end(), [](const auto &a, const auto &b) {
            return a.nickname.localeAwareCompare(b.nickname) < 0;
        });
        QVariantList clientList;
        int streamCount = 0;
        for (const ClientInfo &c : clients) {
            QVariantList streams;
            for (const StreamInfo &s : m_streams)
                if (s.ownerClid == c.clid)
                    streams.append(streamToVariant(s));
            streamCount += streams.size();
            clientList.append(QVariantMap{{"clid", c.clid}, {"nickname", c.nickname}, {"uid", c.uid}, {"identity", c.identity},
                                          {"me", c.clid == m_ownClid}, {"streams", streams}});
        }
        out.append(QVariantMap{{"cid", ch.cid}, {"name", ch.name}, {"description", ch.description},
                               {"current", ch.cid == own}, {"streamCount", streamCount}, {"clients", clientList}});
    }
    return out;
}

void ConnectionHandler::onNotification(const QString &cmd, const QJsonObject &args)
{
    if (cmd == "cloudview") { applyView(args); return; }
    if (cmd == "cloudconnection") { m_reconnecting = args.value("reconnecting").toBool(); emit changed(); return; }
    if (cmd == QLatin1String("notifycliententerview") || cmd == QLatin1String("notifyclientupdated")) {
        const ClientInfo c = clientFromJson(args.value("client").toObject());
        m_clients.insert(c.clid, c);
        emit changed();
    } else if (cmd == QLatin1String("notifyclientleftview")) {
        m_clients.remove(args.value("clid").toInt());
        emit changed();
    } else if (cmd == QLatin1String("notifyclientmoved")) {
        const int clid = args.value("clid").toInt();
        const int cid = args.value("cid").toInt();
        if (m_clients.contains(clid))
            m_clients[clid].cid = cid;
        emit changed();
        if (clid == m_ownClid)
            emit ownChannelChanged(cid);
    } else if (cmd == QLatin1String("notifystreamstarted")) {
        const StreamInfo s = StreamInfo::fromJson(args.value("stream").toObject());
        m_streams.insert(s.id, s);
        emit streamAdded(s.id);
        emit changed();
    } else if (cmd == QLatin1String("notifystreamupdated")) {
        StreamInfo s = StreamInfo::fromJson(args.value("stream").toObject());
        const auto prev = m_streams.find(s.id);
        // a miniatura chega por notificação própria; preserve-a
        if (prev != m_streams.end() && s.thumbnail.isEmpty() && s.access == prev->access)
            s.thumbnail = prev->thumbnail;
        m_streams.insert(s.id, s);
        emit streamUpdated(s.id);
        emit changed();
    } else if (cmd == QLatin1String("notifystreamthumbnail")) {
        const QString id = args.value("streamIdentifier").toString();
        if (m_streams.contains(id)) {
            m_streams[id].thumbnail = args.value("thumbnail").toString();
            emit streamUpdated(id);
        }
    } else if (cmd == QLatin1String("notifystreamstopped")) {
        const QString id = args.value("streamIdentifier").toString();
        if (m_streams.remove(id))
            emit streamRemoved(id, args.value("reason").toInt());
        emit changed();
    }
}

void ConnectionHandler::reset()
{
    m_reconnecting = false;
    m_loggedIn = false;
    m_ownClid = 0;
    m_channels.clear();
    m_clients.clear();
    m_streams.clear();
    emit connectionChanged();
    emit changed();
}

} // namespace openspeak

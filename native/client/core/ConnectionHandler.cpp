#include "ConnectionHandler.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>

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
            c.value("identity").toString(), c.value("pingMs").isDouble() ? c.value("pingMs").toDouble() : -1,
            c.value("hasProfile").toBool(), c.value("displayName").toString(), c.value("color").toString(),
            c.value("description").toString(), c.value("avatar").toString(), c.value("mediaTag").toString()};
}

ConnectionHandler::ConnectionHandler(QObject *parent) : QObject(parent)
{
    m_ws = new WebSocketConnection(this);
    connect(&m_connection, &ServerConnection::connected, this, &ConnectionHandler::login);
    connect(&m_connection, &ServerConnection::disconnected, this, [this](const QString &reason) {
        const bool was = m_loggedIn;
        // Login recusado já avisou o motivo (ex.: senha errada); a queda que vem
        // em seguida não deve trocá-lo por "bye"/"conexão encerrada".
        const bool rejected = std::exchange(m_loginRejected, false);
        reset();
        if (rejected)
            return;
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
    m_pendingPassword.clear();
    m_loginRejected = false;
    m_connection.setBackend(m_mesh);
    emit connectionChanged();
    m_mesh->start(nickname);
}

void ConnectionHandler::connectTo(const QString &url, const QString &nickname, const QString &uid, const QString &password,
                                  const QString &profilePassword)
{
    m_cloudMode = false;
    m_ws->setCloudMode(false);
    m_pendingNickname = nickname;
    m_pendingUid = uid;
    m_pendingPassword = password;
    m_pendingProfilePassword = profilePassword;
    m_profilePasswordRequired = false;
    m_loginRejected = false;
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
    m_pendingPassword.clear();
    m_loginRejected = false;
    m_ws->setCloudMode(true);
    m_connection.setBackend(m_ws);
    emit connectionChanged();
    m_ws->open(QUrl(QStringLiteral("wss://openspeak-dun.vercel.app/ws")));
}

void ConnectionHandler::login()
{
    QJsonObject args{{"nickname", m_pendingNickname}, {"uid", m_pendingUid}};
    if (!m_pendingPassword.isEmpty())
        args.insert("password", m_pendingPassword);
    if (!m_pendingProfilePassword.isEmpty())
        args.insert("profilePassword", m_pendingProfilePassword);
    m_connection.sendCommand(QStringLiteral("login"), args, [this](const CommandResult &r) {
        if (!r.ok()) {
            m_loginRejected = true;
            m_profilePasswordRequired = r.returnCode == rc::ProfilePassword || r.returnCode == rc::ProfileLocked;
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
    return it != m_clients.end() ? it->shownName() : QStringLiteral("#%1").arg(clid);
}

QString ConnectionHandler::clientIdentity(int clid) const
{
    const auto it = m_clients.find(clid);
    return it != m_clients.end() ? it->identity : QString();
}

void ConnectionHandler::setProfilePassword(const QString &password, const QString &currentPassword)
{
    m_connection.sendCommand(QStringLiteral("setprofilepassword"), {{"password", password}, {"currentPassword", currentPassword}},
                             [this, password](const CommandResult &r) {
        if (r.ok())
            m_pendingProfilePassword = password; // a reconexão usa a senha nova
        emit profileResult(QStringLiteral("password"), r.ok(), r.error.isEmpty() ? r.returnCode : r.error);
    });
}

void ConnectionHandler::updateProfile(const QVariantMap &fields)
{
    m_connection.sendCommand(QStringLiteral("updateprofile"), QJsonObject::fromVariantMap(fields), [this](const CommandResult &r) {
        emit profileResult(QStringLiteral("profile"), r.ok(), r.error.isEmpty() ? r.returnCode : r.error);
    });
}

QString ConnectionHandler::avatarFromFile(const QUrl &file, double x, double y, double side) const
{
    QImageReader reader(file.toLocalFile());
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    // Quadrado de 128 px: cabe com folga no limite do servidor (MaxAvatarBytes).
    const int pixels = std::clamp(int(std::lround(side * image.width())), 1, std::min(image.width(), image.height()));
    const int left = std::clamp(int(std::lround(x * image.width())), 0, image.width() - pixels);
    const int top = std::clamp(int(std::lround(y * image.height())), 0, image.height() - pixels);
    const QImage square = image.copy(left, top, pixels, pixels)
                              .scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB888);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    square.save(&buffer, "JPEG", 80);
    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
}

QVariantMap ConnectionHandler::animatedAvatarFromFile(const QUrl &file) const
{
    QFile in(file.toLocalFile());
    const QImage first = QImage::fromData(in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray());
    if (first.isNull())
        return {};
    // A capa usa o mesmo recorte central com que o GIF aparece.
    const double side = double(std::min(first.width(), first.height()));
    const double x = (first.width() - side) / 2 / first.width();
    const double y = (first.height() - side) / 2 / first.height();
    return {{"avatar", avatarFromFile(file, x, y, side / first.width())}, {"avatarAnimated", bannerFromFile(file)}};
}

QString ConnectionHandler::bannerFromFile(const QUrl &file) const
{
    const QString path = file.toLocalFile();
    if (path.endsWith(QLatin1String(".gif"), Qt::CaseInsensitive)) {
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly))
            return {};
        return QStringLiteral("data:image/gif;base64,") + QString::fromLatin1(in.readAll().toBase64());
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    // Proporção 5:2, o dobro do tamanho no card para ficar nítido em telas com escala.
    const QImage banner = image.scaled(960, 384, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                              .copy(QRect(QPoint(0, 0), QSize(960, 384)))
                              .convertToFormat(QImage::Format_RGB888);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    banner.save(&buffer, "JPEG", 85);
    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
}

bool ConnectionHandler::gifTooLarge(const QUrl &file) const
{
    const QFileInfo info(file.toLocalFile());
    return info.suffix().compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0 && info.size() * 4 / 3 + 32 > MaxProfileMediaBytes;
}

void ConnectionHandler::fetchProfileMedia(int clid, const QString &tag)
{
    if (tag.isEmpty())
        return;
    const auto cached = m_profileMedia.constFind(tag);
    if (cached != m_profileMedia.cend()) {
        emit profileMediaReady(clid, tag, *cached);
        return;
    }
    m_connection.sendCommand(QStringLiteral("profilemedia"), {{"clid", clid}}, [this, clid, tag](const CommandResult &r) {
        if (!r.ok())
            return;
        if (m_profileMedia.size() >= 32)
            m_profileMedia.clear(); // mídia é pesada; quem reabrir um card baixa de novo
        const QVariantMap media = r.data.toVariantMap();
        m_profileMedia.insert(tag, media);
        emit profileMediaReady(clid, tag, media);
    });
}

QVariantMap ConnectionHandler::ownProfile() const
{
    const ClientInfo *c = client(m_ownClid);
    if (!c)
        return {};
    return {{"nickname", c->nickname}, {"hasProfile", c->hasProfile}, {"displayName", c->displayName},
            {"color", c->color}, {"description", c->description}, {"avatar", c->avatar},
            {"clid", c->clid}, {"mediaTag", c->mediaTag}};
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
            return a.shownName().localeAwareCompare(b.shownName()) < 0;
        });
        QVariantList clientList;
        int streamCount = 0;
        for (const ClientInfo &c : clients) {
            QVariantList streams;
            for (const StreamInfo &s : m_streams)
                if (s.ownerClid == c.clid)
                    streams.append(streamToVariant(s));
            streamCount += streams.size();
            clientList.append(QVariantMap{{"clid", c.clid}, {"nickname", c.nickname}, {"name", c.shownName()}, {"uid", c.uid}, {"identity", c.identity},
                                          {"me", c.clid == m_ownClid}, {"streams", streams}, {"pingMs", c.pingMs},
                                          {"avatar", c.avatar}, {"description", c.description}, {"color", c.color},
                                          {"mediaTag", c.mediaTag}});
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
    if (cmd == "notifyclientlatency") {
        const int clid = args.value("clid").toInt();
        if (m_clients.contains(clid)) {
            m_clients[clid].pingMs = args.value("pingMs").isDouble() ? args.value("pingMs").toDouble() : -1;
            emit changed();
        }
        return;
    }
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

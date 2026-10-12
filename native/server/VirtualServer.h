#pragma once
// Servidor virtual: canais, clientes e registro de streams.
// Porta em C++ de server/virtualServer.js. Não toca em mídia: guarda o estado,
// aplica privacidade/limites e repassa `streamsignaling` entre os pares.

#include "Protocol.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <functional>
#include <map>
#include <stdexcept>

namespace openspeak {

class CommandError : public std::runtime_error {
public:
    CommandError(QString code, const QString &message = {})
        : std::runtime_error((message.isEmpty() ? code : message).toStdString()), code(std::move(code)) {}
    QString code;
};

class VirtualServer {
public:
    using SendFn = std::function<void(int clid, const QJsonObject &message)>;

    VirtualServer(SendFn send, QString name = QStringLiteral("OpenSpeak"), QJsonArray iceServers = {});

    int connect();
    void disconnect(int clid);

    /// Executa um comando; devolve `data` da resposta ou lança CommandError.
    QJsonObject handle(int clid, const QString &cmd, const QJsonObject &args);

    QString name() const { return m_name; }
    int streamCount() const { return int(m_streams.size()); }

private:
    struct Client {
        int clid = 0;
        bool loggedIn = false;
        QString nickname;
        QString uid;
        int cid = 0;
    };
    struct Channel {
        int cid = 0;
        int order = 0;
        QString name;
        QString description;
    };
    struct Quality {
        int width = 1280;
        int height = 720;
        int fps = 30;
    };
    struct PendingRequest {
        QJsonArray codecs;
        qint64 since = 0;
    };
    struct Stream {
        QString id;
        int ownerClid = 0;
        int cid = 0;
        int mode = int(StreamMode::P2P);
        qint64 created = 0;
        bool paused = false;
        QString thumbnail;
        QString name;
        int type = int(StreamType::Screen);
        int access = int(StreamAccess::Public);
        int bitrate = 3'000'000;
        int viewerLimit = 0;
        bool audio = false;
        Quality quality;
        QList<int> viewers; // ordem de entrada
        std::map<int, PendingRequest> pending;
        QSet<QString> banned; // uids
    };

    using Handler = QJsonObject (VirtualServer::*)(Client &, const QJsonObject &);
    static const QHash<QString, Handler> &handlers();

    QJsonObject cmdLogin(Client &, const QJsonObject &);
    QJsonObject cmdClientMove(Client &, const QJsonObject &);
    QJsonObject cmdClientUpdate(Client &, const QJsonObject &);
    QJsonObject cmdSetupStream(Client &, const QJsonObject &);
    QJsonObject cmdUpdateStream(Client &, const QJsonObject &);
    QJsonObject cmdStopStream(Client &, const QJsonObject &);
    QJsonObject cmdStreamThumbnail(Client &, const QJsonObject &);
    QJsonObject cmdRequestStreamInfo(Client &, const QJsonObject &);
    QJsonObject cmdJoinStreamRequest(Client &, const QJsonObject &);
    QJsonObject cmdRespondJoinStreamRequest(Client &, const QJsonObject &);
    QJsonObject cmdRemoveClientFromStream(Client &, const QJsonObject &);
    QJsonObject cmdStreamUnban(Client &, const QJsonObject &);
    QJsonObject cmdStreamSignaling(Client &, const QJsonObject &);

    void applyConfig(Stream &s, const QJsonObject &args);
    Stream &stream(const QJsonValue &id);
    Stream &ownStream(const Client &c, const QJsonValue &id);
    void ban(Stream &s, const QString &uid);
    void removeViewer(Stream &s, int clid, LeaveReason reason);
    void stopStream(QString id, LeaveReason reason);
    void detachFromStreams(const Client &c, LeaveReason reason);
    void notifyAttendees(const Stream &s);

    QJsonObject publicClient(const Client &c) const;
    QJsonObject publicStream(const Stream &s) const;
    void notify(int clid, const QString &cmd, const QJsonObject &args);
    void broadcast(const QString &cmd, const QJsonObject &args, int exceptClid = 0);
    void broadcastToChannel(int cid, const QString &cmd, const QJsonObject &args, int exceptClid = 0);

    SendFn m_send;
    QString m_name;
    QJsonArray m_iceServers;
    std::map<int, Client> m_clients;
    std::map<int, Channel> m_channels;
    std::map<QString, Stream> m_streams;
    int m_nextClid = 1;
    int m_nextStreamId = 1;
    int m_defaultChannel = 1;
};

} // namespace openspeak

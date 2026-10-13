#pragma once
// Espelho local do estado do servidor (canais, clientes, streams anunciados).

#include "Protocol.h"
#include "MeshConnection.h"
#include "ServerConnection.h"

#include <QJsonArray>
#include <QMap>
#include <QObject>
#include <QVariantList>

namespace openspeak {

struct ChannelInfo {
    int cid = 0;
    int order = 0;
    QString name;
    QString description;
};

struct ClientInfo {
    int clid = 0;
    int cid = 0;
    QString nickname;
    QString uid;
    QString identity; // identidade real (Tailscale), discreta na UI
    double pingMs = -1;
};

struct StreamQuality {
    int width = 1280;
    int height = 720;
    int fps = 30;
};

struct StreamInfo {
    QString id;
    int ownerClid = 0;
    int cid = 0;
    QString name;
    StreamType type = StreamType::Screen;
    StreamAccess access = StreamAccess::Public;
    int bitrate = 0;
    int viewerLimit = 0;
    bool audio = false;
    StreamQuality quality;
    bool paused = false;
    qint64 created = 0;
    int viewerCount = 0;
    QString thumbnail; // data URL

    static StreamInfo fromJson(const QJsonObject &o);
};

class ConnectionHandler : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(QString serverName READ serverName NOTIFY connectionChanged)
    Q_PROPERTY(int ownClid READ ownClid NOTIFY connectionChanged)
    Q_PROPERTY(double serverPingMs READ serverPingMs NOTIFY changed)
    Q_PROPERTY(int ownChannel READ ownChannel NOTIFY changed)
    Q_PROPERTY(bool hasStreamingP2P READ hasStreamingP2P NOTIFY connectionChanged)
    Q_PROPERTY(bool hasStreamingSFU READ hasStreamingSFU NOTIFY connectionChanged)
    Q_PROPERTY(QVariantList tree READ tree NOTIFY changed)
    Q_PROPERTY(bool meshMode READ meshMode NOTIFY connectionChanged)
    Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY changed)
    Q_PROPERTY(QString tailnetStatus READ tailnetStatus NOTIFY tailnetChanged)
    Q_PROPERTY(QString tailnetName READ tailnetName NOTIFY tailnetChanged)
    Q_PROPERTY(QString tailnetIdentity READ tailnetIdentity NOTIFY tailnetChanged)
    Q_PROPERTY(int tailnetOnline READ tailnetOnline NOTIFY tailnetChanged)
public:
    explicit ConnectionHandler(QObject *parent = nullptr);
    ~ConnectionHandler() override { m_connection.close(); }
    bool reconnecting() const { return m_reconnecting; }
    bool cloudMode() const { return m_cloudMode; }

    ServerConnection *connection() { return &m_connection; }

    /// Modo tailnet (sem servidor): `directory` fornece os pares.
    void setDirectory(PeerDirectory *directory);
    PeerDirectory *directory() const { return m_directory; }

    Q_INVOKABLE void connectTailnet(const QString &nickname);
    Q_INVOKABLE void connectTo(const QString &url, const QString &nickname, const QString &uid);
    Q_INVOKABLE void connectCloud(const QString &nickname, const QString &uid);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void moveTo(int cid);
    Q_INVOKABLE QString clientName(int clid) const;
    Q_INVOKABLE QString clientIdentity(int clid) const;

    bool isConnected() const { return m_loggedIn; }
    QString serverName() const { return m_serverName; }
    int ownClid() const { return m_ownClid; }
    double serverPingMs() const { const auto *c = client(m_ownClid); return c ? c->pingMs : -1; }
    int ownChannel() const;
    bool hasStreamingP2P() const { return m_capabilities.contains(CapStreamP2P); }
    bool hasStreamingSFU() const { return m_capabilities.contains(CapStreamSFU); }
    bool meshMode() const { return m_mesh != nullptr && m_connection.backend() == m_mesh; }
    QString tailnetStatus() const;
    QString tailnetName() const { return m_directory ? m_directory->networkName() : QString(); }
    QString tailnetIdentity() const { return m_directory ? m_directory->self().identityLabel() : QString(); }
    int tailnetOnline() const;
    QJsonArray iceServers() const { return m_iceServers; }

    const QMap<QString, StreamInfo> &streams() const { return m_streams; }
    const StreamInfo *stream(const QString &id) const;
    QList<StreamInfo> streamsInChannel(int cid) const;
    const ClientInfo *client(int clid) const;
    const QMap<int, ClientInfo> &clients() const { return m_clients; }

    /// Árvore para a QML: [{cid, name, current, clients:[{clid, nickname, me, streams:[...]}]}]
    QVariantList tree() const;

signals:
    void connectionChanged();
    void connectFailed(const QString &error);
    void disconnected(const QString &reason);
    void changed();
    void streamAdded(const QString &id);
    void streamUpdated(const QString &id);
    void streamRemoved(const QString &id, int reason);
    void ownChannelChanged(int cid);
    void tailnetChanged();

private:
    void onNotification(const QString &cmd, const QJsonObject &args);
    void reset();
    void login();
    void applyView(const QJsonObject &data);

    ConnectionRouter m_connection;
    WebSocketConnection *m_ws = nullptr;
    MeshConnection *m_mesh = nullptr;
    PeerDirectory *m_directory = nullptr;
    bool m_loggedIn = false;
    bool m_reconnecting = false, m_cloudMode = false;
    QString m_serverName;
    QStringList m_capabilities;
    QJsonArray m_iceServers;
    int m_ownClid = 0;
    QMap<int, ChannelInfo> m_channels;
    QMap<int, ClientInfo> m_clients;
    QMap<QString, StreamInfo> m_streams;
    QString m_pendingNickname, m_pendingUid;
};

QVariantMap streamToVariant(const StreamInfo &s);

} // namespace openspeak

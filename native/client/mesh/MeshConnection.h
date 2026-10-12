#pragma once
// Modo descentralizado: sem servidor. Cada app:
//  - descobre os outros pela tailnet (PeerDirectory / `tailscale status`);
//  - abre uma conexão WebSocket direta com cada par (malha completa);
//  - é a AUTORIDADE sobre o próprio estado (sala atual, streams, quem assiste,
//    bans, limite) — exatamente as decisões que o dono já tomava no TS;
//  - publica esse estado para os pares e repassa pedidos/sinalização direto
//    para o dono do stream.
//
// Para o motor (ConnectionHandler/ScreenshareManager) isto é indistinguível do
// servidor dedicado: mesmos comandos, mesmas notificações. A identidade real de
// cada par vem do Tailscale (pelo IP de origem), não do que o par declara.

#include "PeerDirectory.h"
#include "ServerConnection.h"

#include <QJsonArray>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

namespace openspeak {

class MeshConnection : public ServerConnection {
    Q_OBJECT
public:
    static constexpr quint16 DefaultPort = 9987;
    static constexpr int ProtocolVersion = 1;

    MeshConnection(PeerDirectory *directory, QObject *parent = nullptr);
    ~MeshConnection() override;

    /// Começa a escutar no IP da tailnet e a procurar pares.
    void start(const QString &nickname);
    void close() override;
    bool isConnected() const override { return m_running; }
    void sendCommand(const QString &cmd, const QJsonObject &args = {}, Callback callback = {}) override;

    PeerDirectory *directory() const { return m_dir; }
    int connectedPeers() const;

private:
    struct OwnStream {
        QString id;
        int cid = 1;
        QString name;
        int type = 2, access = 1, mode = 1, bitrate = 3'000'000, viewerLimit = 0;
        bool audio = false, paused = false;
        int width = 1280, height = 720, fps = 30;
        qint64 created = 0;
        QString thumbnail;
        QList<int> viewers;
        QMap<int, QJsonArray> pending; // clid -> codecs
        QSet<QString> banned;          // identidade Tailscale
    };
    struct Peer {
        PeerEntry entry;
        QPointer<QWebSocket> socket;
        bool ready = false;
        int clid = 0;
        QString nickname;
        int cid = 1;
        QMap<QString, QJsonObject> streams; // id -> info pública (como o par anunciou)
        qint64 retryAt = 0;
    };
    struct JoinWait {
        Callback callback;
        QString cmd;
        QTimer *timer = nullptr;
    };

    // comandos (emulação do servidor)
    QJsonObject handle(const QString &cmd, const QJsonObject &args);
    QJsonObject cmdLogin();
    QJsonObject cmdClientMove(const QJsonObject &a);
    QJsonObject cmdSetupStream(const QJsonObject &a);
    QJsonObject cmdUpdateStream(const QJsonObject &a);
    QJsonObject cmdStopStream(const QJsonObject &a);
    QJsonObject cmdThumbnail(const QJsonObject &a);
    QJsonObject cmdRespondJoin(const QJsonObject &a);
    QJsonObject cmdRemoveClient(const QJsonObject &a);
    QJsonObject cmdUnban(const QJsonObject &a);
    QJsonObject cmdSignaling(const QJsonObject &a);
    void cmdJoinRequest(const QJsonObject &a, Callback callback);

    // malha
    void maintain();
    void connectTo(const PeerEntry &entry);
    void attachSocket(QWebSocket *ws, const QString &key);
    void onMessage(QWebSocket *ws, const QString &text);
    void onPeerMessage(Peer &peer, const QJsonObject &m);
    void applyState(Peer &peer, const QJsonObject &m);
    void dropPeer(const QString &key, const QString &why);
    void sendTo(Peer &peer, const QJsonObject &m);
    void broadcastState();
    QJsonObject stateMessage(const char *type) const;

    // autoridade sobre os próprios streams
    OwnStream *own(const QString &id);
    void applyConfig(OwnStream &s, const QJsonObject &a);
    void removeViewer(OwnStream &s, int clid, int reason, bool notifyPeer);
    QJsonObject publicOwnStream(const OwnStream &s) const;
    QJsonObject remoteStreamFor(const Peer &peer, QJsonObject info) const;

    // auxiliares
    Peer *peerByClid(int clid);
    Peer *ownerOf(const QString &streamId, QString *outKey = nullptr);
    int clidFor(const QString &key);
    QJsonObject clientInfo(const Peer &peer) const;
    QJsonObject selfInfo() const;
    void emitLocal(const QString &cmd, const QJsonObject &args);
    void notifyAttendees(const OwnStream &s);

    PeerDirectory *m_dir;
    QWebSocketServer m_server;
    QTimer m_maintain;
    bool m_running = false;
    QString m_nickname;
    int m_cid = 1;
    QString m_selfTag;
    int m_nextStream = 1;
    int m_nextClid = 2;
    int m_nextJoin = 1;
    QMap<QString, int> m_clidByKey;
    QMap<QString, Peer> m_peers;               // key -> par
    QMap<QWebSocket *, QString> m_socketKey;   // socket identificado -> key
    QSet<QWebSocket *> m_unidentified;         // aguardando hello
    QMap<QString, OwnStream> m_own;
    QMap<int, JoinWait> m_joinWaits;
};

} // namespace openspeak

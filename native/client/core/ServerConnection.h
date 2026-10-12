#pragma once
// Canal de comandos/notificações (protocolo OpenSpeak).
//
// `ServerConnection` é a interface usada pelo motor (ConnectionHandler,
// ScreenshareManager). Implementações:
//   - WebSocketConnection: servidor dedicado (openspeak-server / server/index.js)
//   - MeshConnection:      sem servidor; cada app é a autoridade de si mesmo e
//                          conversa direto com os outros na tailnet
//   - ConnectionRouter:    encaminha para a implementação ativa

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <functional>

namespace openspeak {

struct CommandResult {
    QString cmd;
    QString returnCode; // "ok" em caso de sucesso
    QString error;
    QJsonObject data;
    bool ok() const { return returnCode == QLatin1String("ok"); }
};

class ServerConnection : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const CommandResult &)>;
    using QObject::QObject;

    virtual void close() = 0;
    virtual bool isConnected() const = 0;
    /// Envia um comando; `callback` recebe sucesso ou erro (inclusive timeout).
    virtual void sendCommand(const QString &cmd, const QJsonObject &args = {}, Callback callback = {}) = 0;

signals:
    void connected();
    void disconnected(const QString &reason);
    void notification(const QString &cmd, const QJsonObject &args);
};

/// Servidor dedicado via WebSocket.
class WebSocketConnection : public ServerConnection {
    Q_OBJECT
public:
    explicit WebSocketConnection(QObject *parent = nullptr);

    void open(const QUrl &url);
    void close() override;
    bool isConnected() const override;
    void sendCommand(const QString &cmd, const QJsonObject &args = {}, Callback callback = {}) override;

private:
    struct Pending {
        QString cmd;
        Callback callback;
        QTimer *timer = nullptr;
    };

    void onText(const QString &text);
    void failAll(const QString &code);

    QWebSocket m_ws;
    QHash<int, Pending> m_pending;
    int m_nextId = 1;
    bool m_wasOpen = false;
};

/// Encaminha para o backend ativo (o motor guarda um ponteiro estável).
class ConnectionRouter : public ServerConnection {
    Q_OBJECT
public:
    using ServerConnection::ServerConnection;

    void setBackend(ServerConnection *backend);
    ServerConnection *backend() const { return m_backend; }

    void close() override;
    bool isConnected() const override;
    void sendCommand(const QString &cmd, const QJsonObject &args = {}, Callback callback = {}) override;

private:
    QPointer<ServerConnection> m_backend;
    QList<QMetaObject::Connection> m_links;
};

} // namespace openspeak

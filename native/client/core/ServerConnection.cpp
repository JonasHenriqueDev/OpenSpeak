#include "ServerConnection.h"

#include <QJsonDocument>

namespace openspeak {

namespace {
constexpr int CommandTimeoutMs = 15'000;
}

// ===================================================== WebSocketConnection

WebSocketConnection::WebSocketConnection(QObject *parent) : ServerConnection(parent)
{
    m_ws.setMaxAllowedIncomingMessageSize(4 * 1024 * 1024);
    connect(&m_ws, &QWebSocket::connected, this, [this] {
        m_wasOpen = true;
        emit connected();
    });
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &WebSocketConnection::onText);
    connect(&m_ws, &QWebSocket::disconnected, this, [this] {
        failAll(QStringLiteral("disconnected"));
        const QString reason = m_wasOpen ? m_ws.closeReason() : m_ws.errorString();
        m_wasOpen = false;
        emit disconnected(reason);
    });
}

void WebSocketConnection::open(const QUrl &url) { m_ws.open(url); }

void WebSocketConnection::close() { m_ws.close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("bye")); }

bool WebSocketConnection::isConnected() const { return m_ws.state() == QAbstractSocket::ConnectedState; }

void WebSocketConnection::sendCommand(const QString &cmd, const QJsonObject &args, Callback callback)
{
    if (!isConnected()) {
        if (callback)
            callback({cmd, QStringLiteral("disconnected"), QStringLiteral("sem conexão"), {}});
        return;
    }
    const int id = m_nextId++;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
        Pending p = m_pending.take(id);
        if (p.timer)
            p.timer->deleteLater();
        if (p.callback)
            p.callback({p.cmd, QStringLiteral("timeout"), QStringLiteral("tempo esgotado em %1").arg(p.cmd), {}});
    });
    timer->start(CommandTimeoutMs);
    m_pending.insert(id, Pending{cmd, std::move(callback), timer});
    const QJsonObject msg{{"type", "cmd"}, {"id", id}, {"cmd", cmd}, {"args", args}};
    m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void WebSocketConnection::onText(const QString &text)
{
    const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString type = msg.value("type").toString();
    if (type == QLatin1String("res")) {
        auto it = m_pending.find(msg.value("id").toInt());
        if (it == m_pending.end())
            return;
        Pending p = std::move(it.value());
        m_pending.erase(it);
        p.timer->deleteLater();
        if (p.callback)
            p.callback({p.cmd, msg.value("return_code").toString(), msg.value("error").toString(), msg.value("data").toObject()});
    } else if (type == QLatin1String("notify")) {
        emit notification(msg.value("cmd").toString(), msg.value("args").toObject());
    }
}

void WebSocketConnection::failAll(const QString &code)
{
    const auto pending = std::exchange(m_pending, {});
    for (const Pending &p : pending) {
        p.timer->deleteLater();
        if (p.callback)
            p.callback({p.cmd, code, QStringLiteral("conexão encerrada"), {}});
    }
}

// ======================================================== ConnectionRouter

void ConnectionRouter::setBackend(ServerConnection *backend)
{
    for (const auto &c : std::as_const(m_links))
        disconnect(c);
    m_links.clear();
    m_backend = backend;
    if (!backend)
        return;
    m_links.append(connect(backend, &ServerConnection::connected, this, &ServerConnection::connected));
    m_links.append(connect(backend, &ServerConnection::disconnected, this, &ServerConnection::disconnected));
    m_links.append(connect(backend, &ServerConnection::notification, this, &ServerConnection::notification));
}

void ConnectionRouter::close()
{
    if (m_backend)
        m_backend->close();
}

bool ConnectionRouter::isConnected() const { return m_backend && m_backend->isConnected(); }

void ConnectionRouter::sendCommand(const QString &cmd, const QJsonObject &args, Callback callback)
{
    if (m_backend) {
        m_backend->sendCommand(cmd, args, std::move(callback));
        return;
    }
    if (callback)
        callback({cmd, QStringLiteral("disconnected"), QStringLiteral("sem conexão"), {}});
}

} // namespace openspeak

#include "ServerConnection.h"

#include <QJsonDocument>
#include <QSysInfo>

namespace openspeak {

namespace {
constexpr int CommandTimeoutMs = 15'000;
// Ping próprio: sem pong nesse tempo a conexão está morta (a rede some sem
// fechar o TCP). Derrubar e retomar a sessão é mais rápido que esperar o TCP.
constexpr int LivenessPingMs = 5'000;
constexpr int LivenessTimeoutMs = 15'000;
}

// ===================================================== WebSocketConnection

WebSocketConnection::WebSocketConnection(QObject *parent) : ServerConnection(parent)
{
    m_ws.setMaxAllowedIncomingMessageSize(4 * 1024 * 1024);
    connect(&m_ws, &QWebSocket::connected, this, [this] {
        m_wasOpen = true;
        m_sincePong.start();
        m_liveness.start();
        if (!m_resumeToken.isEmpty()) {
            auto args = m_loginArgs;
            args.insert("resumeClid", m_resumeClid);
            args.insert("resumeToken", m_resumeToken);
            sendCommand("login", args, [this](const CommandResult &r) {
                if (r.ok()) {
                    m_retryMs = 1000;
                    emit notification("cloudview", r.data);
                    emit notification("cloudconnection", {{"reconnecting", false}});
                } else if (r.returnCode == "session_expired") {
                    m_resumeToken.clear();
                    auto fresh = m_loginArgs;
                    fresh.remove("resumeClid"); fresh.remove("resumeToken");
                    sendCommand("login", fresh, [this](const CommandResult &result) {
                        if (result.ok()) {
                            m_retryMs = 1000;
                            auto view = result.data;
                            view.insert("restarted", true);
                            emit notification("cloudview", view);
                            emit notification("cloudconnection", {{"reconnecting", false}});
                        } else { m_stopped = true; m_ws.close(); emit disconnected(result.error); }
                    });
                } else if (r.returnCode == "access_denied") {
                    m_stopped = true; m_ws.close(); emit disconnected(r.error);
                } else m_ws.close();
            });
            return;
        }
        emit connected();
    });
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &WebSocketConnection::onText);
    connect(&m_ws, &QWebSocket::disconnected, this, [this] {
        m_liveness.stop();
        failAll(QStringLiteral("disconnected"));
        const QString reason = m_wasOpen ? m_ws.closeReason() : m_ws.errorString();
        m_wasOpen = false;
        // Com sessão retomável (sala web ou servidor nativo) a queda não tira
        // ninguém da sala: reconecta e retoma o mesmo clid, mídia incluída.
        if (!m_stopped && !m_resumeToken.isEmpty()) {
            emit notification("cloudconnection", {{"reconnecting", true}});
            m_retry.start(m_retryMs);
            m_retryMs = qMin(m_retryMs * 2, 10'000);
            return;
        }
        emit disconnected(reason);
    });
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { if (!m_stopped) m_ws.open(m_url); });
    m_heartbeat.setInterval(30'000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        if (isConnected() && !m_resumeToken.isEmpty()) sendCommand("sync", {}, [this](const CommandResult &r) {
            if (r.ok()) emit notification("cloudview", r.data);
            else m_ws.abort();
        });
    });
    m_liveness.setInterval(LivenessPingMs);
    connect(&m_ws, &QWebSocket::pong, this, [this] { m_sincePong.start(); });
    connect(&m_liveness, &QTimer::timeout, this, [this] {
        if (!isConnected())
            return;
        if (m_sincePong.elapsed() > LivenessTimeoutMs) {
            m_ws.abort();
            return;
        }
        m_ws.ping();
    });
}

void WebSocketConnection::setCloudMode(bool enabled) { m_cloud = enabled; }
void WebSocketConnection::open(const QUrl &url) {
    m_url = url; m_stopped = false; m_retryMs = 1000;
    m_resumeToken.clear(); m_retry.stop(); m_heartbeat.stop();
    m_ws.open(url);
}

void WebSocketConnection::close() {
    m_stopped = true; m_retry.stop(); m_heartbeat.stop(); m_liveness.stop();
    if (isConnected() && !m_resumeToken.isEmpty()) {
        const QJsonObject msg{{"type", "cmd"}, {"id", m_nextId++}, {"cmd", "logout"}, {"args", QJsonObject{}}};
        m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
        m_ws.flush();
    }
    m_resumeToken.clear(); m_loginArgs = {};
    m_ws.close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("bye"));
}

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
    QJsonObject outgoing = args;
    if (cmd == "login") {
        outgoing["clientInfo"] = QJsonObject{{"name", "OpenSpeak"}, {"version", QStringLiteral(OPENSPEAK_VERSION)},
                                             {"kind", "native"}, {"platform", QSysInfo::prettyProductName()}};
        outgoing["resumable"] = true;
    }
    // Servidor que devolve resumeToken (sala web ou servidor nativo novo) permite
    // retomar a sessão depois de uma queda.
    if (cmd == "login" && !args.contains("resumeToken")) {
        m_loginArgs = outgoing;
        callback = [this, next = std::move(callback)](const CommandResult &r) {
            if (r.ok()) {
                m_resumeToken = r.data.value("resumeToken").toString();
                m_resumeClid = r.data.value("clid").toInt();
                // A sala web precisa do sync para manter a sessão (lease).
                if (m_cloud && !m_resumeToken.isEmpty())
                    m_heartbeat.start();
            }
            if (next) next(r);
        };
    }
    m_pending.insert(id, Pending{cmd, std::move(callback), timer});
    const QJsonObject msg{{"type", "cmd"}, {"id", id}, {"cmd", cmd}, {"args", outgoing}};
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

#include "ServerHost.h"

#include <QFileInfo>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUrl>

#ifdef OPENSPEAK_HAS_SFU
#include "SfuRelay.h"
#endif

Q_LOGGING_CATEGORY(lcServer, "openspeak.server")

namespace openspeak {

namespace {
constexpr qint64 MaxMessageBytes = 512 * 1024;
}

ServerHost::ServerHost(Options options, QObject *parent)
    : QObject(parent),
      m_options(std::move(options)),
      m_server([this](int clid, const QJsonObject &msg) { sendTo(clid, msg); }, m_options.name, m_options.iceServers)
{
#ifdef OPENSPEAK_HAS_SFU
    if (m_options.sfu) {
        SfuRelay::Options relay;
        relay.iceServers = m_options.iceServers;
        relay.transport.udpMuxPort = m_options.rtcPort;
        relay.transport.publicIp = m_options.publicIp;
        m_relay = std::make_unique<SfuRelay>(relay);
        m_server.setRelay(m_relay.get());
    }
#else
    if (m_options.sfu)
        qCWarning(lcServer) << "relay SFU indisponível: compilado sem libdatachannel";
#endif
    m_http.addWebSocketUpgradeVerifier(this, [](const QHttpServerRequest &request) {
        return request.url().path() == QLatin1String("/ws") ? QHttpServerWebSocketUpgradeResponse::accept()
                                                            : QHttpServerWebSocketUpgradeResponse::deny();
    });
    connect(&m_http, &QHttpServer::newWebSocketConnection, this, &ServerHost::onNewWebSocket);

    if (!m_options.webRoot.isEmpty()) {
        const QDir root(m_options.webRoot);
        auto serve = [root](const QString &relative) -> QHttpServerResponse {
            const QString clean = QDir::cleanPath(relative.isEmpty() ? QStringLiteral("index.html") : relative);
            const QString file = root.absoluteFilePath(clean);
            if (clean.startsWith(QLatin1String("..")) || !file.startsWith(root.absolutePath()) || !QFileInfo(file).isFile())
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
            QHttpServerResponse response = QHttpServerResponse::fromFile(file);
            QHttpHeaders headers = response.headers();
            headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-cache");
            if (file.endsWith(QLatin1String(".js")))
                headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::ContentType, "text/javascript; charset=utf-8");
            response.setHeaders(std::move(headers));
            return response;
        };
        m_http.route(QStringLiteral("/"), [serve] { return serve({}); });
        m_http.route(QStringLiteral("/<arg>"), [serve](const QUrl &path) { return serve(path.path()); });
    }

    m_heartbeat.setInterval(15'000);
    connect(&m_heartbeat, &QTimer::timeout, this, &ServerHost::heartbeat);
}

bool ServerHost::start()
{
    m_tcp = new QTcpServer(this);
    if (!m_tcp->listen(m_options.address, m_options.port) || !m_http.bind(m_tcp)) {
        qCCritical(lcServer) << "não foi possível escutar na porta" << m_options.port << m_tcp->errorString();
        return false;
    }
    m_heartbeat.start();
    return true;
}

quint16 ServerHost::port() const { return m_tcp ? m_tcp->serverPort() : 0; }

bool ServerHost::sfuAvailable()
{
#ifdef OPENSPEAK_HAS_SFU
    return true;
#else
    return false;
#endif
}

void ServerHost::onNewWebSocket()
{
    while (m_http.hasPendingWebSocketConnections()) {
        QWebSocket *ws = m_http.nextPendingWebSocketConnection().release();
        ws->setParent(this);
        ws->setMaxAllowedIncomingMessageSize(MaxMessageBytes);
        const int clid = m_server.connect();
        m_sockets.insert(clid, ws);
        m_alive.insert(ws, true);
        qCInfo(lcServer).noquote() << "conexão clid=" << clid << "de" << ws->peerAddress().toString();

        connect(ws, &QWebSocket::textMessageReceived, this, [this, clid](const QString &text) { onMessage(clid, text); });
        connect(ws, &QWebSocket::pong, this, [this, ws] { m_alive[ws] = true; });
        connect(ws, &QWebSocket::disconnected, this, [this, ws, clid] {
            m_sockets.remove(clid);
            m_alive.remove(ws);
            m_server.disconnect(clid);
            ws->deleteLater();
            qCInfo(lcServer) << "desconectado clid=" << clid;
        });
    }
}

void ServerHost::onMessage(int clid, const QString &text)
{
    const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
    if (msg.value("type").toString() != QLatin1String("cmd") || !msg.value("cmd").isString())
        return;
    QJsonObject reply{{"type", "res"}, {"id", msg.value("id")}};
    try {
        reply["data"] = m_server.handle(clid, msg.value("cmd").toString(), msg.value("args").toObject());
        reply["return_code"] = rc::Ok;
    } catch (const CommandError &err) {
        reply["return_code"] = err.code;
        reply["error"] = QString::fromStdString(err.what());
    } catch (const std::exception &err) {
        qCWarning(lcServer) << "erro em" << msg.value("cmd").toString() << err.what();
        reply["return_code"] = rc::Internal;
        reply["error"] = QStringLiteral("erro interno");
    }
    sendTo(clid, reply);
}

void ServerHost::sendTo(int clid, const QJsonObject &message)
{
    if (QWebSocket *ws = m_sockets.value(clid))
        ws->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void ServerHost::heartbeat()
{
    // Derruba sockets mortos para liberar streams presos.
    for (auto it = m_alive.begin(); it != m_alive.end(); ++it) {
        if (!it.value()) {
            it.key()->abort();
            continue;
        }
        it.value() = false;
        it.key()->ping();
    }
}

} // namespace openspeak

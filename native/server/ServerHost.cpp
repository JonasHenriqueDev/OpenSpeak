#include "ServerHost.h"

#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QSaveFile>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QLoggingCategory>
#include <QUrl>
#include <QNetworkReply>
#include <QNetworkRequest>

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
      m_server([this](int clid, const QJsonObject &msg) { sendTo(clid, msg); }, m_options.name, m_options.iceServers),
      m_discord(m_options.discordClientId, m_options.discordClientSecret)
{
#ifdef OPENSPEAK_HAS_SFU
    if (m_options.sfu) {
        SfuRelay::Options relay;
        relay.iceServers = m_options.iceServers;
        relay.transport.udpMuxPort = m_options.rtcPort;
        relay.transport.publicIp = m_options.publicIp;
        m_relay = std::make_unique<SfuRelay>(relay);
        m_relay->setBinarySender([this](int clid, const QByteArray &data) -> qint64 {
            QWebSocket *ws = m_sockets.value(clid);
            if (!ws)
                return -1;
            ws->sendBinaryMessage(data);
            m_mediaTx += quint64(data.size());
            return ws->bytesToWrite();
        });
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

    // Atividade do Discord: código OAuth2 -> access token (o secret não sai do servidor).
    // Registrada antes das rotas do cliente web, que aceitam qualquer caminho.
    m_http.route(QStringLiteral("/api/discord/token"), QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request, QHttpServerResponder &responder) {
        auto shared = std::make_shared<QHttpServerResponder>(std::move(responder));
        const QJsonObject body = request.body().size() <= 4096 ? QJsonDocument::fromJson(request.body()).object() : QJsonObject{};
        m_discord.exchange(body.value("code").toString(), [shared](int status, const QJsonObject &reply) {
            shared->write(QJsonDocument(reply), QHttpServerResponder::StatusCode(status));
        });
    });

    // Bot do Discord: transmissões ao vivo marcadas para anunciar (só leitura).
    m_http.route(QStringLiteral("/api/live"), QHttpServerRequest::Method::Get, [this] {
        QHttpServerResponse response(m_server.liveSnapshot());
        QHttpHeaders headers = response.headers();
        headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
        response.setHeaders(std::move(headers));
        return response;
    });

    if (!m_options.webRoot.isEmpty()) {
        const QDir root(m_options.webRoot);
        auto serve = [root](QString relative) -> QHttpServerResponse {
            // "v/<versão>/..." é o mesmo arquivo: a atividade do Discord usa um caminho
            // novo a cada publicação porque o proxy do Discord guarda JS/CSS em cache.
            static const QRegularExpression versionPrefix(QStringLiteral("^v/[A-Za-z0-9._-]+/"));
            relative.remove(versionPrefix);
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

    // Bot do Discord: avisa na hora em vez de esperar a próxima consulta ao /api/live.
    m_liveWebhook = QUrl(QString::fromLocal8Bit(qgetenv("OPENSPEAK_LIVE_WEBHOOK")));
    m_liveSecret = qgetenv("OPENSPEAK_LIVE_WEBHOOK_SECRET");
    if (m_liveWebhook.isValid() && m_liveWebhook.scheme().startsWith(QLatin1String("http"))) {
        m_liveDebounce.setSingleShot(true);
        m_liveDebounce.setInterval(300); // junta mudanças seguidas num POST só
        connect(&m_liveDebounce, &QTimer::timeout, this, &ServerHost::sendLiveWebhook);
        m_server.setLiveChangedCallback([this] { if (!m_liveDebounce.isActive()) m_liveDebounce.start(); });
        qCInfo(lcServer).noquote() << "webhook do bot:" << m_liveWebhook.toString(QUrl::RemoveUserInfo);
    }

    m_heartbeat.setInterval(15'000);
    connect(&m_heartbeat, &QTimer::timeout, this, &ServerHost::heartbeat);
    m_monitoringFile = QString::fromLocal8Bit(qgetenv("OPENSPEAK_METRICS_FILE"));
    if (!m_monitoringFile.isEmpty()) {
        m_monitoring.setInterval(2'000);
        connect(&m_monitoring, &QTimer::timeout, this, &ServerHost::writeMonitoringSnapshot);
    }
}

bool ServerHost::start()
{
    m_tcp = new QTcpServer(this);
    if (!m_tcp->listen(m_options.address, m_options.port) || !m_http.bind(m_tcp)) {
        qCCritical(lcServer) << "não foi possível escutar na porta" << m_options.port << m_tcp->errorString();
        return false;
    }
    m_heartbeat.start();
    if (!m_monitoringFile.isEmpty()) {
        writeMonitoringSnapshot();
        m_monitoring.start();
    }
    return true;
}

void ServerHost::writeMonitoringSnapshot()
{
    auto data = m_server.monitoringSnapshot();
    data["timestamp"] = double(QDateTime::currentMSecsSinceEpoch());
    data["pid"] = double(QCoreApplication::applicationPid());
    data["sfuEnabled"] = sfuEnabled();
    data["signalingRxBytes"] = double(m_signalingRx);
    data["signalingTxBytes"] = double(m_signalingTx);
    data["websocketMediaTxBytes"] = double(m_mediaTx);
    QJsonArray clients;
    for (const auto &value : data.value("clients").toArray()) {
        auto client = value.toObject();
        const auto info = m_connectionInfo.value(client.value("clid").toInt());
        client["ip"] = info.ip;
        client["userAgent"] = info.userAgent;
        client["origin"] = info.origin;
        client["connectedAt"] = double(info.connectedAt);
        client["lastActivityAt"] = double(info.lastActivityAt);
        client["pingRttMs"] = info.pingRttMs < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(double(info.pingRttMs));
        client["signalingRxBytes"] = double(info.rxBytes);
        client["signalingTxBytes"] = double(info.txBytes);
        clients.append(client);
    }
    data["clients"] = clients;
    if (m_relay)
        data["mediaSessions"] = m_relay->monitoringSessions();
    QSaveFile file(m_monitoringFile);
    const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        qCWarning(lcServer) << "não foi possível gravar o monitoramento:" << file.errorString();
}

void ServerHost::sendLiveWebhook()
{
    const QByteArray body = QJsonDocument(m_server.liveSnapshot()).toJson(QJsonDocument::Compact);
    if (body == m_lastLive)
        return; // só a contagem de algo que o bot não mostra, ou nada mudou
    m_lastLive = body;
    QNetworkRequest request(m_liveWebhook);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setTransferTimeout(5'000);
    if (!m_liveSecret.isEmpty())
        request.setRawHeader("X-OpenSpeak-Secret", m_liveSecret);
    QNetworkReply *reply = m_liveNetwork.post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 300) {
            m_lastLive.clear(); // a próxima mudança reenvia tudo (o bot também consulta o /api/live)
            qCWarning(lcServer).noquote() << "webhook do bot falhou:" << (status ? QString::number(status) : reply->errorString());
        }
    });
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
        ConnectionInfo info;
        info.connectedAt = info.lastActivityAt = QDateTime::currentMSecsSinceEpoch();
        info.ip = ws->peerAddress().toString();
        const auto request = ws->request();
        // Só o proxy local é confiável para informar o IP; clientes diretos não podem forjar XFF.
        if (ws->peerAddress().isLoopback()) {
            const auto forwarded = QString::fromLatin1(request.rawHeader("X-Forwarded-For")).split(',').last().trimmed();
            const QHostAddress address(forwarded);
            if (!address.isNull()) info.ip = address.toString();
        }
        info.userAgent = QString::fromUtf8(request.rawHeader("User-Agent")).left(512);
        info.origin = QString::fromUtf8(request.rawHeader("Origin")).left(256);
        m_connectionInfo.insert(clid, info);
        qCInfo(lcServer).noquote() << "conexão clid=" << clid << "de" << ws->peerAddress().toString();

        connect(ws, &QWebSocket::textMessageReceived, this, [this, clid](const QString &text) { onMessage(clid, text); });
        connect(ws, &QWebSocket::pong, this, [this, ws, clid](quint64 elapsedMs, const QByteArray &) {
            m_alive[ws] = true;
            auto &info = m_connectionInfo[clid];
            info.pingRttMs = qint64(elapsedMs);
            info.lastActivityAt = QDateTime::currentMSecsSinceEpoch();
            m_server.setClientLatency(clid, info.pingRttMs);
        });
        connect(ws, &QWebSocket::disconnected, this, [this, ws, clid] {
            m_sockets.remove(clid);
            m_alive.remove(ws);
            m_connectionInfo.remove(clid);
            m_server.disconnect(clid);
            ws->deleteLater();
            qCInfo(lcServer) << "desconectado clid=" << clid;
        });
        ws->ping();
    }
}

void ServerHost::onMessage(int clid, const QString &text)
{
    m_signalingRx += text.toUtf8().size();
    auto &info = m_connectionInfo[clid];
    info.rxBytes += text.toUtf8().size();
    info.lastActivityAt = QDateTime::currentMSecsSinceEpoch();
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
    // Measure after login as well: native peers may still be opening when the
    // handshake ping arrives. The reply establishes the client's public view.
    if (msg.value("cmd").toString() == QLatin1String("login") && reply.value("return_code").toString() == rc::Ok)
        if (auto *ws = m_sockets.value(clid)) ws->ping();
}

void ServerHost::sendTo(int clid, const QJsonObject &message)
{
    if (QWebSocket *ws = m_sockets.value(clid)) {
        const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
        m_signalingTx += bytes.size();
        m_connectionInfo[clid].txBytes += bytes.size();
        ws->sendTextMessage(QString::fromUtf8(bytes));
    }
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

// openspeak-server: servidor de sinalização (equivalente ao server/index.js).
//
//   openspeak-server [--port 9988] [--name OpenSpeak] [--web <pasta do cliente web>]
//   OPENSPEAK_ICE='[{"urls":"stun:..."}]' define os iceServers anunciados.

#include "ServerHost.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>

using namespace openspeak;

static QJsonArray iceServersFromEnv()
{
    const QByteArray env = qgetenv("OPENSPEAK_ICE");
    if (!env.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(env);
        if (doc.isArray())
            return doc.array();
        qWarning("OPENSPEAK_ICE inválido, usando padrão");
    }
    return QJsonArray{QJsonObject{{"urls", "stun:stun.l.google.com:19302"}}};
}

static QString defaultWebRoot()
{
    // Procura a pasta public/ do cliente web subindo a partir do executável.
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("public/index.html"))))
            return dir.filePath(QStringLiteral("public"));
        if (!dir.cdUp())
            break;
    }
    return {};
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("openspeak-server"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Servidor de sinalização OpenSpeak (STREAM_P2P)"));
    parser.addHelpOption();
    QCommandLineOption portOpt(QStringLiteral("port"), QStringLiteral("Porta (padrão 9988)."), QStringLiteral("porta"), QStringLiteral("9988"));
    QCommandLineOption nameOpt(QStringLiteral("name"), QStringLiteral("Nome do servidor."), QStringLiteral("nome"), QStringLiteral("OpenSpeak"));
    QCommandLineOption webOpt(QStringLiteral("web"), QStringLiteral("Pasta do cliente web a servir (opcional)."), QStringLiteral("pasta"));
    QCommandLineOption noWebOpt(QStringLiteral("no-web"), QStringLiteral("Não servir o cliente web."));
    QCommandLineOption hostOpt(QStringLiteral("host"), QStringLiteral("Interface de escuta (padrão 0.0.0.0)."), QStringLiteral("ip"), QStringLiteral("0.0.0.0"));
    parser.addOptions({portOpt, nameOpt, webOpt, noWebOpt, hostOpt});
    parser.process(app);

    ServerHost::Options options;
    options.port = quint16(parser.value(portOpt).toUInt());
    options.address = QHostAddress(parser.value(hostOpt));
    if (options.address.isNull()) { qCritical("Interface de escuta inválida"); return 1; }
    options.name = parser.value(nameOpt);
    options.iceServers = iceServersFromEnv();
    if (!parser.isSet(noWebOpt))
        options.webRoot = parser.isSet(webOpt) ? parser.value(webOpt) : defaultWebRoot();

    ServerHost host(options);
    if (!host.start())
        return 1;
    qInfo().noquote() << QStringLiteral("[openspeak] servidor \"%1\" em ws://localhost:%2/ws").arg(options.name).arg(host.port());
    if (!options.webRoot.isEmpty())
        qInfo().noquote() << QStringLiteral("[openspeak] cliente web: http://localhost:%1 (%2)").arg(host.port()).arg(options.webRoot);
    return app.exec();
}

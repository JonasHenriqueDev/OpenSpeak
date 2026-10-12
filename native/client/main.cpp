// OpenSpeak — cliente nativo (Qt Quick + libdatachannel + FFmpeg).
//
//   OpenSpeak.exe [--profile nome] [--debug] [--server ws://host:9988/ws] [--nickname Nome]
//
// --profile separa identidade/configurações (duas instâncias na mesma máquina).
// --debug   habilita a fonte "Padrão de teste" no diálogo de transmissão.

#include "ConnectionHandler.h"
#include "Layout.h"
#include "PeerDirectory.h"
#include "ScreenshareManager.h"
#include "Settings.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStyleHints>
#include <QQuickWindow>
#include <QTimer>

using namespace openspeak;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("OpenSpeak"));
    QGuiApplication::setOrganizationName(QStringLiteral("OpenSpeak"));
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption profileOpt(QStringLiteral("profile"), QStringLiteral("Perfil de configurações."), QStringLiteral("nome"));
    QCommandLineOption debugOpt(QStringLiteral("debug"), QStringLiteral("Habilita a fonte de teste."));
    QCommandLineOption serverOpt(QStringLiteral("server"), QStringLiteral("URL do servidor."), QStringLiteral("url"));
    QCommandLineOption nickOpt(QStringLiteral("nickname"), QStringLiteral("Apelido."), QStringLiteral("nome"));
    QCommandLineOption autoOpt(QStringLiteral("autoconnect"), QStringLiteral("Conecta ao abrir."));
    // Depuração: salva capturas da janela e inicia um stream de teste sozinho.
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Salva capturas da janela (prefixo)."), QStringLiteral("arquivo"));
    QCommandLineOption autoStreamOpt(QStringLiteral("autostream"), QStringLiteral("Inicia um stream de teste (requer --debug)."));
    QCommandLineOption meshPortOpt(QStringLiteral("mesh-port"), QStringLiteral("Porta da malha na tailnet (padrão 9987)."), QStringLiteral("porta"));
    QCommandLineOption openOpt(QStringLiteral("open"), QStringLiteral("Abre um painel ao iniciar: setup | stats | viewers."), QStringLiteral("painel"));
    QCommandLineOption autoJoinOpt(QStringLiteral("autojoin"), QStringLiteral("Assiste ao primeiro stream do canal."));
    parser.addOptions({profileOpt, debugOpt, serverOpt, nickOpt, autoOpt, shotOpt, autoStreamOpt, autoJoinOpt, openOpt, meshPortOpt});
    parser.process(app);

    Settings settings(parser.value(profileOpt), parser.isSet(debugOpt));
    if (parser.isSet(serverOpt)) {
        settings.setServerUrl(parser.value(serverOpt));
        settings.setConnectionMode(QStringLiteral("server"));
    }
    if (parser.isSet(nickOpt))
        settings.setNickname(parser.value(nickOpt));
    ConnectionHandler handler;
    // Modo padrão: descoberta dos pares pela tailnet (sem servidor).
    const quint16 meshPort = parser.isSet(meshPortOpt) ? quint16(parser.value(meshPortOpt).toUInt()) : MeshConnection::DefaultPort;
    handler.setDirectory(new TailscaleDirectory(meshPort, &handler));
    ScreenshareManager screenshare(&handler, &settings);
    StreamLayout layout;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("Server"), &handler);
    engine.rootContext()->setContextProperty(QStringLiteral("Screenshare"), &screenshare);
    engine.rootContext()->setContextProperty(QStringLiteral("StreamLayout"), &layout);
    engine.rootContext()->setContextProperty(QStringLiteral("AutoConnect"), parser.isSet(autoOpt));
    engine.rootContext()->setContextProperty(QStringLiteral("DebugOpen"), parser.value(openOpt));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("OpenSpeak"), QStringLiteral("Main"));

    if (parser.isSet(autoStreamOpt) && settings.debug()) {
        QObject::connect(&handler, &ConnectionHandler::connectionChanged, &screenshare, [&] {
            if (!handler.isConnected())
                return;
            QTimer::singleShot(500, &screenshare, [&] {
                // OPENSPEAK_AUTOSOURCE=screen usa a Tela 1 em vez do padrão de teste.
                if (qgetenv("OPENSPEAK_AUTOSOURCE") == "screen")
                    screenshare.openPreview({{"type", 2}, {"id", "0"}}, 30);
                else
                    screenshare.openPreview({{"type", 2}, {"id", "test"}, {"test", true}}, 30);
                screenshare.startStream({{"name", "Padrão de teste"}, {"access", 1}, {"audio", qgetenv("OPENSPEAK_AUTOAUDIO") == "1"}, {"bitrate", 3'000'000},
                                         {"width", 1280}, {"height", 720}, {"fps", 30}});
            });
        });
    }
    if (parser.isSet(autoJoinOpt)) {
        QObject::connect(&handler, &ConnectionHandler::streamAdded, &screenshare, [&](const QString &id) {
            QTimer::singleShot(300, &screenshare, [&, id] { screenshare.joinStream(id); });
        });
        QObject::connect(&handler, &ConnectionHandler::connectionChanged, &screenshare, [&] {
            if (handler.isConnected() && !handler.streams().isEmpty())
                screenshare.joinStream(handler.streams().firstKey());
        });
    }
    if (parser.isSet(shotOpt)) {
        const QString prefix = parser.value(shotOpt);
        auto *timer = new QTimer(&app);
        int shot = 0;
        QObject::connect(timer, &QTimer::timeout, &app, [&engine, prefix, shot]() mutable {
            if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
                w->grabWindow().save(QStringLiteral("%1-%2.png").arg(prefix).arg(++shot));
        });
        timer->start(4000);
    }
    return app.exec();
}

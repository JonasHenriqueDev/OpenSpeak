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
#include "SourceThumbnails.h"
#include "WindowThumbnail.h"
#include "AutoUpdater.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStyleHints>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace openspeak;

static QQuickItem *findVisualItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisualItem(child, name)) return found;
    return nullptr;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("OpenSpeak"));
    QGuiApplication::setOrganizationName(QStringLiteral("OpenSpeak"));
    QGuiApplication::setApplicationVersion(QStringLiteral(OPENSPEAK_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/openspeak.ico")));
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
    QCommandLineOption openOpt(QStringLiteral("open"), QStringLiteral("Abre um painel ao iniciar: setup | setup-windows | setup-cameras | source | settings | stats | viewers | pip."), QStringLiteral("painel"));
    QCommandLineOption autoJoinOpt(QStringLiteral("autojoin"), QStringLiteral("Assiste ao primeiro stream do canal."));
    QCommandLineOption uiSmokeOpt(QStringLiteral("ui-smoke-test"), QStringLiteral("Valida o layout QML em 900x560 e encerra (sem conectar)."));
    QCommandLineOption cloudOpt(QStringLiteral("cloud"), QStringLiteral("Conecta à sala web pública."));
    QCommandLineOption quitOpt(QStringLiteral("quit-after"), QStringLiteral("Encerra o teste após N segundos (requer --debug)."), QStringLiteral("segundos"));
    QCommandLineOption updateOpt(QStringLiteral("apply-update"), QStringLiteral("Auxiliar interno de atualização."), QStringLiteral("arquivo"));
    QCommandLineOption digestOpt(QStringLiteral("update-sha256"), QStringLiteral("SHA-256 do instalador."), QStringLiteral("hash"));
    QCommandLineOption waitPidOpt(QStringLiteral("wait-pid"), QStringLiteral("Processo que precisa encerrar antes da atualização."), QStringLiteral("pid"));
    QCommandLineOption restartOpt(QStringLiteral("restart-after-update"), QStringLiteral("Reabre o app após atualização solicitada."));
    QCommandLineOption afterUpdateOpt(QStringLiteral("after-update"), QStringLiteral("Aberto pelo instalador ao fim da atualização."));
    parser.addOptions({profileOpt, debugOpt, serverOpt, nickOpt, autoOpt, shotOpt, autoStreamOpt, autoJoinOpt, openOpt, meshPortOpt, uiSmokeOpt, cloudOpt, quitOpt, updateOpt, digestOpt, waitPidOpt, restartOpt, afterUpdateOpt});
    parser.process(app);
    if (parser.isSet(updateOpt))
        return AutoUpdater::runInstallHelper(parser.value(updateOpt), parser.value(digestOpt), parser.value(waitPidOpt).toUInt(), parser.isSet(restartOpt));
#ifdef Q_OS_WIN
    // O instalador segura este mutex (SetupMutex) enquanto roda. Aberto no meio da
    // atualização, o app não usaria arquivos pela metade nem dispararia outra instalação.
    if (!parser.isSet(afterUpdateOpt)) {
        if (HANDLE setup = OpenMutexW(SYNCHRONIZE, FALSE, L"OpenSpeakSetupMutex")) {
            CloseHandle(setup);
            MessageBoxW(nullptr, L"O OpenSpeak está sendo atualizado e vai abrir sozinho quando a instalação terminar.",
                        L"OpenSpeak", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }
#endif

    Settings settings(parser.value(profileOpt), parser.isSet(debugOpt));
    AutoUpdater updater(&settings);
    if (!parser.isSet(debugOpt) && !parser.isSet(uiSmokeOpt))
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &updater, &AutoUpdater::installOnExit);
    if (parser.isSet(uiSmokeOpt)) settings.setAdvancedMode(true);
    if (parser.isSet(serverOpt)) {
        settings.setServerUrl(parser.value(serverOpt));
        settings.setConnectionMode(QStringLiteral("server"));
    }
    if (parser.isSet(nickOpt))
        settings.setNickname(parser.value(nickOpt));
    ConnectionHandler handler;
    // Edição lite: só cliente do servidor (streams P2P ou SFU), sem Tailscale nem sala web.
    const bool lite = updater.edition() == QLatin1String("lite");
    if (!lite) {
        // Modos avançados: descoberta dos pares pela tailnet (sem servidor).
        const quint16 meshPort = parser.isSet(meshPortOpt) ? quint16(parser.value(meshPortOpt).toUInt()) : MeshConnection::DefaultPort;
        handler.setDirectory(new TailscaleDirectory(meshPort, &handler));
    }
    ScreenshareManager screenshare(&handler, &settings);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &handler, &ConnectionHandler::disconnectFromServer);
    if (parser.isSet(quitOpt) && settings.debug())
        QTimer::singleShot(qBound(1, parser.value(quitOpt).toInt(), 3600) * 1000, &app, &QCoreApplication::quit);
    StreamLayout layout;
    SourceThumbnails sourceThumbnails;

    QQmlApplicationEngine engine;
    qmlRegisterType<WindowThumbnail>("OpenSpeak.Native", 1, 0, "WindowThumbnail");
    engine.rootContext()->setContextProperty(QStringLiteral("Settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("Updater"), &updater);
    engine.rootContext()->setContextProperty(QStringLiteral("Server"), &handler);
    engine.rootContext()->setContextProperty(QStringLiteral("Screenshare"), &screenshare);
    engine.rootContext()->setContextProperty(QStringLiteral("StreamLayout"), &layout);
    engine.rootContext()->setContextProperty(QStringLiteral("SourceThumbnails"), &sourceThumbnails);
    engine.rootContext()->setContextProperty(QStringLiteral("AutoConnect"), parser.isSet(autoOpt) && !parser.isSet(cloudOpt));
    engine.rootContext()->setContextProperty(QStringLiteral("DebugOpen"), parser.value(openOpt));
    engine.rootContext()->setContextProperty(QStringLiteral("UiSmoke"), parser.isSet(uiSmokeOpt));
    engine.rootContext()->setContextProperty(QStringLiteral("LiteEdition"), lite);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("OpenSpeak"), QStringLiteral("Main"));

    if (parser.isSet(uiSmokeOpt)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (!window) return 1;
        window->resize(900, 560);
        QTimer::singleShot(4500, &app, [&app, window, panel = parser.value(openOpt)] {
            bool valid = !window->grabWindow().isNull();
            if (panel.startsWith("login")) {
                auto *connectButton = findVisualItem(window->contentItem(), QStringLiteral("serverConnectButton"));
                auto *advanced = findVisualItem(window->contentItem(), QStringLiteral("advancedOptions"));
                auto *address = findVisualItem(window->contentItem(), QStringLiteral("serverAddress"));
                valid = valid && connectButton && connectButton->isVisible() && address && address->isVisible()
                    && advanced && advanced->isVisible() == (panel == "login-advanced");
            } else if (panel == "startup") {
                auto *startup = findVisualItem(window->contentItem(), QStringLiteral("startupScreen"));
                valid = valid && startup && startup->isVisible() && startup->width() >= 800;
            } else if (panel == "updates") {
                auto *dialog = window->findChild<QObject *>(QStringLiteral("updatesDialog"));
                valid = valid && dialog && dialog->property("visible").toBool()
                    && dialog->property("width").toDouble() >= 400
                    && dialog->property("height").toDouble() < window->height();
            } else if (panel != "view-menu") {
                auto *grid = findVisualItem(window->contentItem(), QStringLiteral("sourceGrid"));
                valid = valid && grid && grid->isVisible() && grid->height() >= 120 && grid->width() >= 200;
                if (panel == "setup") {
                    auto *fps = findVisualItem(window->contentItem(), QStringLiteral("fps60"));
                    auto *settingsPane = findVisualItem(window->contentItem(), QStringLiteral("settingsPane"));
                    valid = valid && fps && settingsPane && fps->width() >= 30;
                    if (fps && settingsPane) {
                        valid = valid && fps->mapToItem(settingsPane, QPointF(fps->width(), 0)).x() <= settingsPane->width() + 1;
                        qInfo() << "UI layout:" << "grid" << (grid ? grid->size() : QSizeF()) << "FPS" << fps->width()
                                << "right edge" << fps->mapToItem(settingsPane, QPointF(fps->width(), 0)).x()
                                << "settings width" << settingsPane->width();
                    } else qWarning() << "Missing FPS control or settings pane" << fps << settingsPane;
                }
            } else {
                auto *menu = window->findChild<QObject *>(QStringLiteral("viewMenu"));
                valid = valid && menu && menu->property("visible").toBool() && menu->property("width").toDouble() >= 250;
            }
            if (!valid) qCritical("UI smoke test failed: preview grid, FPS controls, or menu do not fit.");
            app.exit(valid ? 0 : 1);
        });
    }

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
    if (parser.isSet(cloudOpt) && !lite) {
        auto *directory = handler.directory();
        auto *cloudTimer = new QTimer(&app);
        cloudTimer->setInterval(500);
        QObject::connect(cloudTimer, &QTimer::timeout, &app, [&, directory, cloudTimer] {
            if (updater.startupBusy()) return;
            if (directory->status() != PeerDirectory::Status::Running) return;
            cloudTimer->stop();
            handler.connectCloud(settings.nickname(), settings.uid());
        });
        cloudTimer->start();
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

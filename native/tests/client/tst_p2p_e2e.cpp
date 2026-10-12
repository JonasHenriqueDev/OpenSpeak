// Teste ponta a ponta do motor nativo, num único processo:
// servidor C++ <- transmissor (padrão de teste, H.264 + RTP) -> espectador
// (depacketiza, decodifica). Valida sinalização, P2P real via ICE local,
// single-encoding, estatísticas, kick e fim do stream.

#include "ConnectionHandler.h"
#include "ScreenshareManager.h"
#include "ServerHost.h"
#include "Settings.h"

#include <QSignalSpy>
#include <QTest>
#include <QVideoSink>

using namespace openspeak;

class TestP2PEndToEnd : public QObject {
    Q_OBJECT

    static QVariantMap item(const ScreenshareManager &m, const QString &id)
    {
        for (const QVariant &v : m.items())
            if (v.toMap().value("streamIdentifier").toString() == id)
                return v.toMap();
        return {};
    }

private slots:
    void streamFlow()
    {
        if (qEnvironmentVariableIsSet("OPENSPEAK_RTC_LOG"))
            rtc::InitLogger(rtc::LogLevel::Debug);
        ServerHost::Options opts;
        opts.port = 0;
        opts.address = QHostAddress::LocalHost;
        opts.iceServers = {}; // apenas candidatos locais
        ServerHost host(opts);
        QVERIFY(host.start());
        const QString url = QStringLiteral("ws://127.0.0.1:%1/ws").arg(host.port());

        Settings settingsA(QStringLiteral("e2e-host"), true), settingsB(QStringLiteral("e2e-viewer"), true);
        settingsA.setShowInactiveStreams(true);
        settingsB.setShowInactiveStreams(true);
        settingsB.setHiddenStreams({});
        ConnectionHandler a, b;
        ScreenshareManager ma(&a, &settingsA), mb(&b, &settingsB);
        a.connectTo(url, QStringLiteral("Host"), settingsA.uid());
        b.connectTo(url, QStringLiteral("Viewer"), settingsB.uid());
        QTRY_VERIFY_WITH_TIMEOUT(a.isConnected() && b.isConnected(), 5000);
        QVERIFY(a.hasStreamingP2P());

        // Transmissor: padrão de teste 1280x720, privado (exige aceite manual).
        QVERIFY(ma.openPreview({{"type", 2}, {"id", "test"}, {"test", true}}, 30));
        QSignalSpy joinSpy(&ma, &ScreenshareManager::joinRequest);
        ma.startStream({{"name", "E2E"}, {"access", 3}, {"audio", false}, {"bitrate", 2'000'000},
                        {"width", 1280}, {"height", 720}, {"fps", 30}});
        QTRY_COMPARE_WITH_TIMEOUT(b.streams().size(), 1, 5000);
        const QString id = b.streams().firstKey();

        // Espectador pede para entrar; transmissor aceita.
        mb.joinStream(id);
        QTRY_COMPARE_WITH_TIMEOUT(item(mb, id).value("status").toString(), QStringLiteral("waiting"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(joinSpy.count(), 1, 3000);
        ma.respondJoinRequest(id, b.ownClid(), true);

        QVideoSink sink;
        int frames = 0;
        QSize lastSize;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &f) {
            frames++;
            lastSize = f.size();
        });
        QTRY_VERIFY_WITH_TIMEOUT(item(mb, id).value("status").toString() != QLatin1String("waiting"), 5000);
        mb.attachVideo(id, &sink);

        QTRY_COMPARE_WITH_TIMEOUT(item(mb, id).value("status").toString(), QStringLiteral("playing"), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(frames >= 15, 20000);
        QCOMPARE(lastSize, QSize(1280, 720));
        qInfo() << "quadros recebidos:" << frames;

        // Estatísticas dos dois lados.
        QTest::qWait(2500);
        const QVariantMap out = ma.stats(id, 0);
        const QVariantMap in = mb.stats(id, 0);
        qInfo() << "envio:" << out.value("video").toMap().value("sentPackets") << "pacotes,"
                << out.value("video").toMap().value("encoder").toString()
                << " recebimento:" << in.value("video").toMap().value("receivedPackets") << "pacotes,"
                << in.value("video").toMap().value("decoder").toString();
        QVERIFY(out.value("video").toMap().value("sentPackets").toLongLong() > 0);
        QVERIFY(in.value("video").toMap().value("receivedPackets").toLongLong() > 0);
        QCOMPARE(out.value("viewers").toList().size(), 1);
        QVERIFY(!out.value("video").toMap().value("connectionType").toString().isEmpty());

        // Kick: o espectador vê o erro.
        ma.removeViewer(id, b.ownClid(), false);
        QTRY_COMPARE_WITH_TIMEOUT(item(mb, id).value("errorKey").toString(), QStringLiteral("video.join.error.kicked"), 3000);

        // Fim do stream.
        ma.stopStream(id);
        QTRY_VERIFY_WITH_TIMEOUT(b.streams().isEmpty(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(mb.items().isEmpty(), 3000);
    }
};

QTEST_MAIN(TestP2PEndToEnd)
#include "tst_p2p_e2e.moc"

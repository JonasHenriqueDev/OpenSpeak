// Malha descentralizada ponta a ponta, sem servidor: dois nós (127.0.0.1 em
// portas diferentes) se descobrem pelo diretório, trocam estado, e um assiste
// ao stream do outro com P2P real. Valida identidade verificada, pedido de
// entrada, vídeo, ban, troca de sala e saída da tailnet.

#include "ConnectionHandler.h"
#include "PeerDirectory.h"
#include "ScreenshareManager.h"
#include "Settings.h"

#include <QSignalSpy>
#include <QTest>
#include <QVideoSink>

using namespace openspeak;

class TestMeshEndToEnd : public QObject {
    Q_OBJECT

    static PeerEntry entry(quint16 port, const QString &host, const QString &login, const QString &display)
    {
        PeerEntry e;
        e.address = QHostAddress::LocalHost;
        e.port = port;
        e.hostName = host;
        e.userLogin = login;
        e.userDisplay = display;
        e.online = true;
        return e;
    }

    static QVariantMap item(const ScreenshareManager &m, const QString &id)
    {
        for (const QVariant &v : m.items())
            if (v.toMap().value("streamIdentifier").toString() == id)
                return v.toMap();
        return {};
    }

private slots:
    void meshFlow()
    {
        const PeerEntry ea = entry(47101, "PC-ANA", "ana@exemplo.com", "Ana Real");
        const PeerEntry eb = entry(47102, "PC-BIA", "bia@exemplo.com", "Bia Real");
        auto *dirA = new StaticDirectory(ea, {eb});
        auto *dirB = new StaticDirectory(eb, {ea});

        Settings sa(QStringLiteral("mesh-a"), true), sb(QStringLiteral("mesh-b"), true);
        sb.setHiddenStreams({});
        ConnectionHandler a, b;
        a.setDirectory(dirA);
        b.setDirectory(dirB);
        dirA->setParent(&a);
        dirB->setParent(&b);
        ScreenshareManager ma(&a, &sa), mb(&b, &sb);

        // Nomes de exibição livres; a identidade real vem do diretório.
        a.connectTailnet(QStringLiteral("Transmissora"));
        b.connectTailnet(QStringLiteral("Espectadora"));
        QTRY_VERIFY_WITH_TIMEOUT(a.isConnected() && b.isConnected(), 5000);
        QVERIFY(a.meshMode());
        QTRY_COMPARE_WITH_TIMEOUT(a.clients().size(), 2, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(b.clients().size(), 2, 8000);

        int anaOnB = 0;
        for (const ClientInfo &c : b.clients())
            if (c.nickname == QLatin1String("Transmissora"))
                anaOnB = c.clid;
        QVERIFY(anaOnB > 0);
        QCOMPARE(b.clientIdentity(anaOnB), QStringLiteral("Ana Real · pc-ana"));
        QCOMPARE(b.client(anaOnB)->uid, QStringLiteral("ana@exemplo.com/pc-ana"));

        // Stream privado: exige aceite manual.
        QVERIFY(ma.openPreview({{"type", 2}, {"id", "test"}, {"test", true}}, 30));
        QSignalSpy joinSpy(&ma, &ScreenshareManager::joinRequest);
        ma.startStream({{"name", "Malha"}, {"access", 3}, {"audio", false}, {"bitrate", 2'000'000},
                        {"width", 1280}, {"height", 720}, {"fps", 30}});
        QTRY_COMPARE_WITH_TIMEOUT(b.streams().size(), 1, 5000);
        const QString id = b.streams().firstKey();
        QCOMPARE(b.streams().first().ownerClid, anaOnB);

        mb.joinStream(id);
        QTRY_COMPARE_WITH_TIMEOUT(joinSpy.count(), 1, 5000);
        const int biaOnA = joinSpy.first().at(1).toInt();
        QCOMPARE(a.clientIdentity(biaOnA), QStringLiteral("Bia Real · pc-bia"));
        ma.respondJoinRequest(id, biaOnA, true);

        QVideoSink sink;
        int frames = 0;
        QSize lastSize;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &f) {
            frames++;
            lastSize = f.size();
        });
        QTRY_VERIFY_WITH_TIMEOUT(item(mb, id).value("status").toString() != QLatin1String("waiting"), 5000);
        mb.attachVideo(id, &sink);
        QTRY_VERIFY_WITH_TIMEOUT(frames >= 15, 20000);
        QCOMPARE(lastSize, QSize(1280, 720));
        QTRY_COMPARE_WITH_TIMEOUT(b.streams().value(id).viewerCount, 1, 5000);
        qInfo() << "quadros pela malha:" << frames;

        // Ban pela identidade real: não pode pedir de novo.
        ma.removeViewer(id, biaOnA, true);
        QTRY_COMPARE_WITH_TIMEOUT(item(mb, id).value("errorKey").toString(), QStringLiteral("video.join.error.banned"), 5000);
        mb.joinStream(id);
        QTRY_COMPARE_WITH_TIMEOUT(item(mb, id).value("errorKey").toString(), QStringLiteral("video.join.error.banned"), 5000);
        QCOMPARE(ma.viewers(id).value("banned").toList().size(), 1);

        // Trocar de sala encerra o stream para os outros.
        a.moveTo(2);
        QTRY_VERIFY_WITH_TIMEOUT(b.streams().isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(b.client(anaOnB)->cid, 2, 5000);

        // Saiu da tailnet: some da lista.
        dirB->setPeers({});
        QTRY_COMPARE_WITH_TIMEOUT(b.clients().size(), 1, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(a.clients().size(), 1, 8000);
    }
};

QTEST_MAIN(TestMeshEndToEnd)
#include "tst_mesh_e2e.moc"

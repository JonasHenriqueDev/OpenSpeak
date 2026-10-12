// Mesmos cenários de test/server.test.js, contra a porta C++.

#include "VirtualServer.h"

#include <QTest>

using namespace openspeak;

class TestVirtualServer : public QObject {
    Q_OBJECT

    std::unique_ptr<VirtualServer> server;
    QHash<int, QList<QJsonObject>> inbox;

    int login(const QString &nick)
    {
        const int clid = server->connect();
        inbox[clid];
        server->handle(clid, "login", {{"nickname", nick}, {"uid", "uid-" + nick}});
        return clid;
    }

    QList<QJsonObject> notes(int clid, const QString &cmd) const
    {
        QList<QJsonObject> out;
        for (const QJsonObject &m : inbox.value(clid))
            if (m.value("cmd").toString() == cmd)
                out.append(m.value("args").toObject());
        return out;
    }

    QString expectError(int clid, const QString &cmd, const QJsonObject &args)
    {
        try {
            server->handle(clid, cmd, args);
        } catch (const CommandError &e) {
            return e.code;
        }
        return QStringLiteral("<sem erro>");
    }

    QString setup(int host, const QJsonObject &args = {})
    {
        return server->handle(host, "setupstream", args).value("streamIdentifier").toString();
    }

private slots:
    void init()
    {
        inbox.clear();
        server = std::make_unique<VirtualServer>([this](int clid, const QJsonObject &m) { inbox[clid].append(m); });
    }

    void fullFlow()
    {
        const int host = login("host"), viewer = login("viewer");
        const QString sid = setup(host, {{"name", "Tela"}, {"access", int(StreamAccess::Private)}});
        QCOMPARE(notes(viewer, "notifystreamstarted").size(), 1);

        server->handle(viewer, "joinstreamrequest", {{"streamIdentifier", sid}, {"codecs", QJsonArray{"video/H264"}}});
        const auto req = notes(host, "notifyjoinstreamrequest");
        QCOMPARE(req.size(), 1);
        QCOMPARE(req[0].value("clid").toInt(), viewer);
        QCOMPARE(req[0].value("codecs").toArray().first().toString(), QStringLiteral("video/H264"));

        const QJsonObject offer{{"type", "offer"}, {"sdp", "v=0"}};
        QCOMPARE(expectError(host, "streamsignaling", {{"streamIdentifier", sid}, {"clid", viewer}, {"payload", offer}}), rc::PermissionDenied);

        server->handle(host, "respondjoinstreamrequest", {{"streamIdentifier", sid}, {"clid", viewer}, {"accept", true}});
        QVERIFY(notes(viewer, "notifyrespondjoinstreamrequest")[0].value("accepted").toBool());
        QCOMPARE(notes(host, "notifystreamclientjoined")[0].value("clid").toInt(), viewer);

        server->handle(host, "streamsignaling", {{"streamIdentifier", sid}, {"clid", viewer}, {"payload", offer}});
        const auto sig = notes(viewer, "notifystreamsignaling");
        QCOMPARE(sig[0].value("clid").toInt(), host);
        QCOMPARE(sig[0].value("payload").toObject().value("type").toString(), QStringLiteral("offer"));

        server->handle(viewer, "removeclientfromstream", {{"streamIdentifier", sid}});
        QCOMPARE(notes(host, "notifystreamclientleft")[0].value("reason").toInt(), int(LeaveReason::Left));
    }

    void banAndUnban()
    {
        const int host = login("host"), viewer = login("viewer");
        const QString sid = setup(host);
        server->handle(viewer, "joinstreamrequest", {{"streamIdentifier", sid}});
        server->handle(host, "respondjoinstreamrequest", {{"streamIdentifier", sid}, {"clid", viewer}, {"accept", false}, {"ban", true}});
        QCOMPARE(notes(viewer, "notifyrespondjoinstreamrequest")[0].value("reason").toInt(), int(LeaveReason::Banned));
        QCOMPARE(expectError(viewer, "joinstreamrequest", {{"streamIdentifier", sid}}), rc::Banned);
        server->handle(host, "streamunban", {{"streamIdentifier", sid}, {"uid", "uid-viewer"}});
        server->handle(viewer, "joinstreamrequest", {{"streamIdentifier", sid}});
    }

    void kickWithBan()
    {
        const int host = login("host"), viewer = login("viewer");
        const QString sid = setup(host);
        server->handle(viewer, "joinstreamrequest", {{"streamIdentifier", sid}});
        server->handle(host, "respondjoinstreamrequest", {{"streamIdentifier", sid}, {"clid", viewer}, {"accept", true}});
        server->handle(host, "removeclientfromstream", {{"streamIdentifier", sid}, {"clid", viewer}, {"reason", int(LeaveReason::Banned)}});
        QCOMPARE(notes(viewer, "notifystreamclientleft")[0].value("reason").toInt(), int(LeaveReason::Banned));
        QCOMPARE(notes(host, "notifystreambanadded").size(), 1);
        QCOMPARE(expectError(viewer, "joinstreamrequest", {{"streamIdentifier", sid}}), rc::Banned);
    }

    void viewerCannotRemoveOthers()
    {
        const int host = login("host"), a = login("a"), b = login("b");
        const QString sid = setup(host);
        for (int c : {a, b}) {
            server->handle(c, "joinstreamrequest", {{"streamIdentifier", sid}});
            server->handle(host, "respondjoinstreamrequest", {{"streamIdentifier", sid}, {"clid", c}, {"accept", true}});
        }
        QCOMPARE(expectError(a, "removeclientfromstream", {{"streamIdentifier", sid}, {"clid", b}}), rc::PermissionDenied);
        QCOMPARE(expectError(a, "streamsignaling", {{"streamIdentifier", sid}, {"clid", b}, {"payload", QJsonObject{{"type", "ice"}}}}),
                 rc::PermissionDenied);
    }

    void viewerLimit()
    {
        const int host = login("host"), a = login("a"), b = login("b");
        const QString sid = setup(host, {{"viewerLimit", 1}});
        server->handle(a, "joinstreamrequest", {{"streamIdentifier", sid}});
        server->handle(b, "joinstreamrequest", {{"streamIdentifier", sid}});
        server->handle(host, "respondjoinstreamrequest", {{"streamIdentifier", sid}, {"clid", a}, {"accept", true}});
        QCOMPARE(expectError(host, "respondjoinstreamrequest", {{"streamIdentifier", sid}, {"clid", b}, {"accept", true}}), rc::ViewerLimit);
    }

    void leavingChannelStopsStreams()
    {
        const int host = login("host"), viewer = login("viewer");
        const QString s1 = setup(host);
        server->handle(host, "clientmove", {{"cid", 2}});
        QCOMPARE(notes(viewer, "notifystreamstopped")[0].value("streamIdentifier").toString(), s1);
        const QString s2 = setup(host);
        server->disconnect(host);
        QCOMPARE(notes(viewer, "notifystreamstopped")[1].value("streamIdentifier").toString(), s2);
        QCOMPARE(server->streamCount(), 0);
    }

    void joinRequiresSameChannel()
    {
        const int host = login("host"), viewer = login("viewer");
        const QString sid = setup(host);
        server->handle(viewer, "clientmove", {{"cid", 3}});
        QCOMPARE(expectError(viewer, "joinstreamrequest", {{"streamIdentifier", sid}}), rc::NotInChannel);
    }

    void thumbnailOnlyWhenPublic()
    {
        const int host = login("host"), viewer = login("viewer");
        const QString sid = setup(host, {{"access", int(StreamAccess::Private)}});
        const QString thumb = QStringLiteral("data:image/jpeg;base64,AAAA");
        QVERIFY(!server->handle(host, "streamthumbnail", {{"streamIdentifier", sid}, {"thumbnail", thumb}}).value("published").toBool());
        server->handle(host, "updatestream", {{"streamIdentifier", sid}, {"access", int(StreamAccess::Public)}});
        QVERIFY(server->handle(host, "streamthumbnail", {{"streamIdentifier", sid}, {"thumbnail", thumb}}).value("published").toBool());
        QCOMPARE(notes(viewer, "notifystreamthumbnail")[0].value("thumbnail").toString(), thumb);
    }

    void sfuRejected()
    {
        const int host = login("host");
        QCOMPARE(expectError(host, "setupstream", {{"mode", 2}}), rc::ModeNotSupported);
    }
};

QTEST_GUILESS_MAIN(TestVirtualServer)
#include "tst_virtualserver.moc"

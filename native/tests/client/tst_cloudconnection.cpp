#include "ServerConnection.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocketServer>
using namespace openspeak;

class TestCloudConnection : public QObject {
    Q_OBJECT
private slots:
    void resumesAndStopsOnClose() {
        QWebSocketServer server("QA", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QPointer<QWebSocket> socket;
        int connections = 0;
        bool resumed = false, logout = false;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            socket = server.nextPendingConnection();
            ++connections;
            connect(socket, &QWebSocket::textMessageReceived, this, [&, ws = socket](const QString &raw) {
                const auto msg = QJsonDocument::fromJson(raw.toUtf8()).object();
                const auto args = msg.value("args").toObject();
                if (msg.value("cmd") == "logout") { logout = true; return; }
                if (msg.value("cmd") != "login") return;
                QVERIFY(!args.contains("accessCode"));
                resumed |= args.value("resumeClid").toInt() == 7 && args.value("resumeToken").toString() == "qa-token";
                const QJsonObject reply{{"type", "res"}, {"id", msg.value("id")}, {"return_code", "ok"},
                    {"data", QJsonObject{{"clid", 7}, {"resumeToken", "qa-token"}}}};
                ws->sendTextMessage(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
            });
        });
        WebSocketConnection client;
        client.setCloudMode(true);
        QSignalSpy connected(&client, &ServerConnection::connected);
        QSignalSpy notifications(&client, &ServerConnection::notification);
        client.open(QUrl(QString("ws://127.0.0.1:%1").arg(server.serverPort())));
        QTRY_COMPARE(connected.size(), 1);
        bool loggedIn = false;
        client.sendCommand("login", {{"nickname", "QA"}, {"uid", "qa"}}, [&](const CommandResult &r) { loggedIn = r.ok(); });
        QTRY_VERIFY(loggedIn);
        socket->close();
        QTRY_VERIFY_WITH_TIMEOUT(resumed, 5000);
        QTRY_VERIFY(notifications.size() >= 3);
        QCOMPARE(connected.size(), 1);
        client.close();
        QTRY_VERIFY(logout);
        QTest::qWait(1200);
        QCOMPARE(connections, 2);
        server.close();
    }
};
QTEST_GUILESS_MAIN(TestCloudConnection)
#include "tst_cloudconnection.moc"

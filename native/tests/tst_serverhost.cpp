// Transporte do servidor com WebSockets de verdade: queda sem logout suspende a
// sessão (o cliente continua na sala) e a retomada entrega o que ficou guardado.

#include "ServerHost.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocket>

#include <memory>

using namespace openspeak;

/// Cliente mínimo: guarda as respostas e notificações recebidas.
class Peer : public QObject {
public:
    QWebSocket ws;
    QList<QJsonObject> messages;
    int nextId = 1;

    explicit Peer(quint16 port)
    {
        connect(&ws, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
            messages.append(QJsonDocument::fromJson(text.toUtf8()).object());
        });
        ws.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(port)));
    }

    QJsonObject command(const QString &cmd, const QJsonObject &args = {})
    {
        const int id = nextId++;
        ws.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "cmd"}, {"id", id}, {"cmd", cmd}, {"args", args}})
                                                 .toJson(QJsonDocument::Compact)));
        QJsonObject reply;
        const bool ok = QTest::qWaitFor([&] {
            for (const QJsonObject &m : messages)
                if (m.value("type") == QLatin1String("res") && m.value("id").toInt() == id) {
                    reply = m;
                    return true;
                }
            return false;
        }, 5000);
        return ok ? reply : QJsonObject{{"return_code", "no_reply"}};
    }

    int indexOf(const QString &cmd) const
    {
        for (int i = 0; i < messages.size(); ++i)
            if (messages[i].value("cmd").toString() == cmd)
                return i;
        return -1;
    }
};

class TestServerHost : public QObject {
    Q_OBJECT

    std::unique_ptr<ServerHost> host;
    quint16 port = 0;

private slots:
    void init()
    {
        ServerHost::Options options;
        options.port = 0;
        options.address = QHostAddress::LocalHost;
        host = std::make_unique<ServerHost>(options);
        QVERIFY(host->start());
        port = host->port();
    }

    void cleanup() { host.reset(); }

    void dropWithoutLogoutSuspendsAndResumes()
    {
        Peer alice(port), bob(port);
        QTRY_VERIFY(alice.ws.state() == QAbstractSocket::ConnectedState && bob.ws.state() == QAbstractSocket::ConnectedState);
        const auto login = alice.command("login", {{"nickname", "alice"}, {"uid", "a"}, {"resumable", true}});
        QCOMPARE(login.value("return_code").toString(), rc::Ok);
        const int clid = login.value("data").toObject().value("clid").toInt();
        const QString token = login.value("data").toObject().value("resumeToken").toString();
        QVERIFY(!token.isEmpty());
        QCOMPARE(bob.command("login", {{"nickname", "bob"}, {"uid", "b"}}).value("return_code").toString(), rc::Ok);
        const auto stream = alice.command("setupstream");
        const QString sid = stream.value("data").toObject().value("streamIdentifier").toString();
        QVERIFY(!sid.isEmpty());

        // Queda sem logout (rede caiu): Alice continua na sala, o stream fica pausado.
        bob.messages.clear();
        alice.ws.abort();
        QTRY_VERIFY(bob.indexOf("notifyclientupdated") >= 0);
        QVERIFY(bob.indexOf("notifyclientleftview") < 0);
        QVERIFY(bob.indexOf("notifystreamstopped") < 0);
        // Algo acontece enquanto ela está fora: chega na volta, antes da resposta.
        QCOMPARE(bob.command("clientupdate", {{"nickname", "bobby"}}).value("return_code").toString(), rc::Ok);

        Peer back(port);
        QTRY_VERIFY(back.ws.state() == QAbstractSocket::ConnectedState);
        const auto bad = back.command("login", {{"nickname", "alice"}, {"uid", "a"}, {"resumeClid", clid}, {"resumeToken", "x"}});
        QCOMPARE(bad.value("return_code").toString(), rc::SessionExpired);
        const auto resumed = back.command("login", {{"nickname", "alice"}, {"uid", "a"}, {"resumeClid", clid}, {"resumeToken", token}});
        QCOMPARE(resumed.value("return_code").toString(), rc::Ok);
        const auto view = resumed.value("data").toObject();
        QCOMPARE(view.value("clid").toInt(), clid);
        QVERIFY(view.value("resumed").toBool());
        QCOMPARE(view.value("streams").toArray().size(), 1);
        QVERIFY(!view.value("streams").toArray()[0].toObject().value("paused").toBool());
        const int missed = back.indexOf("notifyclientupdated");
        QVERIFY(missed >= 0);
        QVERIFY(missed < back.messages.indexOf(resumed));
        // A sessão retomada funciona como antes, e o stream continua dela.
        QCOMPARE(back.command("updatestream", {{"streamIdentifier", sid}, {"name", "ainda aqui"}}).value("return_code").toString(), rc::Ok);

        // Logout explícito sai na hora.
        bob.messages.clear();
        back.command("logout");
        QTRY_VERIFY(bob.indexOf("notifyclientleftview") >= 0);
        QVERIFY(bob.indexOf("notifystreamstopped") >= 0);
    }

    void dropWithoutResumableLeavesAtOnce()
    {
        Peer alice(port), bob(port);
        QTRY_VERIFY(alice.ws.state() == QAbstractSocket::ConnectedState && bob.ws.state() == QAbstractSocket::ConnectedState);
        const auto login = alice.command("login", {{"nickname", "alice"}, {"uid", "a"}});
        QVERIFY(!login.value("data").toObject().contains("resumeToken"));
        bob.command("login", {{"nickname", "bob"}, {"uid", "b"}});
        alice.ws.abort();
        QTRY_VERIFY(bob.indexOf("notifyclientleftview") >= 0);
    }
};

QTEST_GUILESS_MAIN(TestServerHost)
#include "tst_serverhost.moc"

#pragma once
// Transporte do servidor: HTTP (cliente web opcional) + WebSocket em /ws na mesma porta.

#include "VirtualServer.h"

#include <QDir>
#include <QHash>
#include <QHttpServer>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QWebSocket>

#include <memory>

namespace openspeak {

class ServerHost : public QObject {
    Q_OBJECT
public:
    struct Options {
        quint16 port = 9988;
        QHostAddress address = QHostAddress::Any;
        QString name = QStringLiteral("OpenSpeak");
        QJsonArray iceServers;
        QString webRoot; // vazio = não serve o cliente web
    };

    explicit ServerHost(Options options, QObject *parent = nullptr);
    bool start();
    quint16 port() const;

private:
    void onNewWebSocket();
    void onMessage(int clid, const QString &text);
    void sendTo(int clid, const QJsonObject &message);
    void heartbeat();

    Options m_options;
    QHttpServer m_http;
    QTcpServer *m_tcp = nullptr;
    VirtualServer m_server;
    QHash<int, QWebSocket *> m_sockets;
    QHash<QWebSocket *, bool> m_alive;
    QTimer m_heartbeat;
};

} // namespace openspeak

#pragma once
// Transporte do servidor: HTTP (cliente web opcional) + WebSocket em /ws na mesma porta.
// Com OPENSPEAK_HAS_SFU, também o relay de mídia (STREAM_SFU) numa porta UDP própria.

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
        bool sfu = false;        // relay de mídia (exige build com OPENSPEAK_HAS_SFU)
        quint16 rtcPort = 10000; // UDP mux do relay (0 = portas efêmeras)
        QString publicIp;        // IP anunciado nos candidatos do relay (NAT 1:1)
    };

    explicit ServerHost(Options options, QObject *parent = nullptr);
    bool start();
    quint16 port() const;
    bool sfuEnabled() const { return m_relay != nullptr; }
    /// Este binário foi compilado com o relay de mídia?
    static bool sfuAvailable();

private:
    void onNewWebSocket();
    void onMessage(int clid, const QString &text);
    void sendTo(int clid, const QJsonObject &message);
    void heartbeat();

    Options m_options;
    QHttpServer m_http;
    QTcpServer *m_tcp = nullptr;
    std::unique_ptr<StreamRelay> m_relay; // declarado antes: vive mais que o VirtualServer
    VirtualServer m_server;
    QHash<int, QWebSocket *> m_sockets;
    QHash<QWebSocket *, bool> m_alive;
    QTimer m_heartbeat;
};

} // namespace openspeak

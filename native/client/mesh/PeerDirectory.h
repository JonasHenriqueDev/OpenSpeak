#pragma once
// Diretório de pares da rede privada (quem pode participar da malha).
//
// TailscaleDirectory consulta `tailscale status --json`: lista as máquinas da
// tailnet, se estão online e QUAL USUÁRIO é dono de cada uma. Essa é a
// identidade real (verificada pelo Tailscale/WireGuard) que fica "por trás"
// do nome de exibição escolhido no app.

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include <optional>

namespace openspeak {

struct PeerEntry {
    QHostAddress address;
    quint16 port = 0;
    QString hostName;    // ex.: DESKTOP-5UG0EPE
    QString userLogin;   // ex.: jonas@gmail.com
    QString userDisplay; // ex.: Jonas Santos
    QString os;
    bool online = false;

    QString key() const { return address.toString() + QLatin1Char(':') + QString::number(port); }
    /// Identidade estável e verificada (usada para ban/contatos).
    QString identity() const { return userLogin + QLatin1Char('/') + hostName.toLower(); }
    /// Texto curto para a UI ("Jonas Santos · desktop-5ug0epe").
    QString identityLabel() const { return userDisplay + QStringLiteral(" · ") + hostName.toLower(); }
};

class PeerDirectory : public QObject {
    Q_OBJECT
public:
    enum class Status { Unknown, Running, NotInstalled, Stopped, NeedsLogin };
    Q_ENUM(Status)

    using QObject::QObject;

    virtual void start() {}
    virtual Status status() const = 0;
    virtual PeerEntry self() const = 0;
    virtual QList<PeerEntry> peers() const = 0;
    virtual QString networkName() const = 0;

    /// Encontra um par pelo IP (+ porta, se informada).
    std::optional<PeerEntry> find(const QHostAddress &address, quint16 port = 0) const;

signals:
    void changed();
};

/// Diretório real via CLI do Tailscale.
class TailscaleDirectory : public PeerDirectory {
    Q_OBJECT
public:
    explicit TailscaleDirectory(quint16 meshPort, QObject *parent = nullptr);

    void start() override;
    Status status() const override { return m_status; }
    PeerEntry self() const override { return m_self; }
    QList<PeerEntry> peers() const override { return m_peers; }
    QString networkName() const override { return m_network; }

    static QString executable();

private:
    void refresh();
    void parse(const QByteArray &json);

    quint16 m_port;
    QTimer m_timer;
    QProcess *m_proc = nullptr;
    Status m_status = Status::Unknown;
    PeerEntry m_self;
    QList<PeerEntry> m_peers;
    QString m_network;
    QByteArray m_lastJsonDigest;
};

/// Diretório fixo (testes, ou rede local sem Tailscale).
class StaticDirectory : public PeerDirectory {
    Q_OBJECT
public:
    StaticDirectory(PeerEntry self, QList<PeerEntry> peers, QObject *parent = nullptr)
        : PeerDirectory(parent), m_self(std::move(self)), m_peers(std::move(peers)) {}

    Status status() const override { return Status::Running; }
    PeerEntry self() const override { return m_self; }
    QList<PeerEntry> peers() const override { return m_peers; }
    QString networkName() const override { return QStringLiteral("local"); }
    void setPeers(QList<PeerEntry> peers)
    {
        m_peers = std::move(peers);
        emit changed();
    }

private:
    PeerEntry m_self;
    QList<PeerEntry> m_peers;
};

} // namespace openspeak

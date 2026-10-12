#include "PeerDirectory.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcDir, "openspeak.tailnet")

namespace openspeak {

namespace {
constexpr int RefreshMs = 5000;

QHostAddress firstIPv4(const QJsonArray &ips)
{
    for (const QJsonValue &v : ips) {
        const QHostAddress a(v.toString());
        if (a.protocol() == QAbstractSocket::IPv4Protocol)
            return a;
    }
    return {};
}
} // namespace

std::optional<PeerEntry> PeerDirectory::find(const QHostAddress &address, quint16 port) const
{
    const QHostAddress v4 = address.protocol() == QAbstractSocket::IPv6Protocol
        ? QHostAddress(address.toIPv4Address()) : address;
    for (const PeerEntry &p : peers()) {
        if ((p.address.isEqual(address) || p.address.isEqual(v4)) && (port == 0 || p.port == port))
            return p;
    }
    return std::nullopt;
}

// ====================================================== TailscaleDirectory

TailscaleDirectory::TailscaleDirectory(quint16 meshPort, QObject *parent) : PeerDirectory(parent), m_port(meshPort)
{
    m_timer.setInterval(RefreshMs);
    connect(&m_timer, &QTimer::timeout, this, &TailscaleDirectory::refresh);
}

QString TailscaleDirectory::executable()
{
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("tailscale"));
    if (!inPath.isEmpty())
        return inPath;
    for (const QString &candidate : {QStringLiteral("C:/Program Files/Tailscale/tailscale.exe"),
                                     QStringLiteral("C:/Program Files (x86)/Tailscale/tailscale.exe")})
        if (QFileInfo::exists(candidate))
            return candidate;
    return {};
}

void TailscaleDirectory::start()
{
    refresh();
    m_timer.start();
}

void TailscaleDirectory::refresh()
{
    if (m_proc)
        return; // consulta anterior ainda em andamento
    const QString exe = executable();
    if (exe.isEmpty()) {
        if (m_status != Status::NotInstalled) {
            m_status = Status::NotInstalled;
            emit changed();
        }
        return;
    }
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QByteArray out = m_proc->readAllStandardOutput();
        m_proc->deleteLater();
        m_proc = nullptr;
        if (out.trimmed().startsWith('{')) {
            parse(out);
        } else if (m_status != Status::Stopped) {
            qCInfo(lcDir) << "tailscale status falhou, código" << code;
            m_status = Status::Stopped;
            m_peers.clear();
            emit changed();
        }
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_proc)
            return;
        m_proc->deleteLater();
        m_proc = nullptr;
        m_status = Status::Stopped;
        emit changed();
    });
    m_proc->start(exe, {QStringLiteral("status"), QStringLiteral("--json")});
}

void TailscaleDirectory::parse(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QString backend = root.value("BackendState").toString();
    const Status status = backend == QLatin1String("Running") ? Status::Running
                        : backend == QLatin1String("NeedsLogin") ? Status::NeedsLogin
                                                                  : Status::Stopped;

    const QJsonObject users = root.value("User").toObject();
    auto entryFrom = [&](const QJsonObject &node) {
        PeerEntry e;
        e.address = firstIPv4(node.value("TailscaleIPs").toArray());
        e.port = m_port;
        e.hostName = node.value("HostName").toString();
        e.os = node.value("OS").toString();
        e.online = node.value("Online").toBool();
        const QJsonObject user = users.value(QString::number(node.value("UserID").toVariant().toLongLong())).toObject();
        e.userLogin = user.value("LoginName").toString();
        e.userDisplay = user.value("DisplayName").toString();
        if (e.userDisplay.isEmpty())
            e.userDisplay = e.userLogin;
        return e;
    };

    PeerEntry self = entryFrom(root.value("Self").toObject());
    self.online = status == Status::Running;
    QList<PeerEntry> peers;
    const QJsonObject peerMap = root.value("Peer").toObject();
    for (auto it = peerMap.begin(); it != peerMap.end(); ++it) {
        PeerEntry e = entryFrom(it.value().toObject());
        if (!e.address.isNull())
            peers.append(e);
    }
    std::sort(peers.begin(), peers.end(), [](const PeerEntry &a, const PeerEntry &b) { return a.key() < b.key(); });

    // Só notifica quando algo relevante mudou (o JSON traz contadores que variam sempre).
    QByteArray digestSource = QByteArray::number(int(status)) + self.key().toUtf8();
    for (const PeerEntry &p : peers)
        digestSource += p.key().toUtf8() + (p.online ? '1' : '0') + p.identity().toUtf8();
    const QByteArray digest = QCryptographicHash::hash(digestSource, QCryptographicHash::Sha1);

    m_status = status;
    m_self = self;
    m_peers = peers;
    m_network = root.value("CurrentTailnet").toObject().value("Name").toString();
    if (digest != m_lastJsonDigest) {
        m_lastJsonDigest = digest;
        emit changed();
    }
}

} // namespace openspeak

#include "AutoUpdater.h"
#include "UpdatePolicy.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace openspeak {
namespace {
QString cachePath() { return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/updates"); }
QNetworkRequest requestFor(const QUrl &url) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "OpenSpeak/" OPENSPEAK_VERSION);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30'000);
    return request;
}
}
AutoUpdater::AutoUpdater(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_network(this),
      m_previousAuto(settings->automaticUpdates()), m_previousPre(settings->prereleaseUpdates()),
      m_previousEdition(settings->updateEdition()) {
    connect(settings, &Settings::changed, this, &AutoUpdater::settingsChanged);
    m_timer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] { if (m_settings->automaticUpdates()) check(); });
    m_timer.start();
    // Every launch checks the selected channel; the setting controls only
    // periodic checks while the app stays open.
    QTimer::singleShot(1000, this, &AutoUpdater::check);
}
QString AutoUpdater::version() const { return QStringLiteral(OPENSPEAK_VERSION); }
QString AutoUpdater::installedEdition() {
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/edition.txt"));
    return file.open(QIODevice::ReadOnly) ? normalizeEdition(QString::fromLatin1(file.read(16))) : QStringLiteral("full");
}
QString AutoUpdater::targetEdition() const {
    const QString chosen = m_settings->updateEdition();
    return chosen.isEmpty() ? m_edition : normalizeEdition(chosen);
}
bool AutoUpdater::installed() const { return QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/unins000.exe")); }
void AutoUpdater::setStatus(const QString &status, bool busy) { m_status = status; m_busy = busy; emit changed(); }
void AutoUpdater::fail(const QString &status) {
    m_startupBusy = false;
    m_installAfterDownload = false;
    setStatus(status);
}
void AutoUpdater::settingsChanged() {
    if (m_previousAuto == m_settings->automaticUpdates() && m_previousPre == m_settings->prereleaseUpdates()
        && m_previousEdition == m_settings->updateEdition()) return;
    const bool editionChanged = m_previousEdition != m_settings->updateEdition();
    m_previousAuto = m_settings->automaticUpdates(); m_previousPre = m_settings->prereleaseUpdates();
    m_previousEdition = m_settings->updateEdition();
    ++m_generation;
    if (m_reply) m_reply->abort();
    m_readyFile.clear(); m_offer = {}; m_busy = false; m_installAfterDownload = false;
    if (m_previousAuto || m_startupBusy || editionChanged) check();
    else setStatus(QStringLiteral("Verificação periódica desativada. O app verifica ao abrir."));
}
void AutoUpdater::check() {
    if (m_busy) return;
    const int generation = ++m_generation;
    m_readyFile.clear(); m_offer = {};
    setStatus(QStringLiteral("Consultando releases do GitHub…"), true);
    const bool pre = m_settings->prereleaseUpdates();
    const QUrl url(pre ? QStringLiteral("https://api.github.com/repos/JonasHenriqueDev/OpenSpeak/releases?per_page=100")
                       : QStringLiteral("https://api.github.com/repos/JonasHenriqueDev/OpenSpeak/releases/latest"));
    auto *reply = m_network.get(requestFor(url)); m_reply = reply;
    auto payload = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, payload] {
        payload->append(reply->readAll());
        if (payload->size() > 2 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, payload, generation] {
        reply->deleteLater();
        if (generation != m_generation) return;
        if (reply->error() != QNetworkReply::NoError) { fail(QStringLiteral("Não foi possível verificar atualizações. Tente novamente mais tarde.")); return; }
        payload->append(reply->readAll());
        const auto doc = QJsonDocument::fromJson(*payload);
        if (doc.isNull() || (!doc.isArray() && !doc.isObject())) {
            fail(QStringLiteral("O GitHub retornou uma resposta inválida. Tente novamente mais tarde.")); return;
        }
        handleReleaseDocument(doc);
    });
}
void AutoUpdater::handleReleaseDocument(const QJsonDocument &doc) {
        const QString target = targetEdition();
        const bool switching = target != m_edition;
        m_offer = selectInstallerRelease(doc.isArray() ? doc.array() : QJsonArray{doc.object()}, m_settings->prereleaseUpdates(), target);
        if (m_offer.isEmpty()) {
            fail(QStringLiteral("Não há um instalador verificado disponível neste canal.")); return;
        }
        // Trocar de edição reinstala a mesma versão; nunca faz downgrade.
        const int order = compareReleaseVersions(m_offer.value("version").toString(), version());
        if (order < 0 || (order == 0 && !switching)) {
            m_offer = {}; m_startupBusy = false;
            setStatus(QStringLiteral("OpenSpeak %1 está atualizado neste canal.").arg(version())); return;
        }
        m_offer.insert("edition", target);
        setStatus(switching ? QStringLiteral("OpenSpeak %1 %2 disponível.").arg(target == QLatin1String("full") ? QStringLiteral("completo") : QStringLiteral("lite"), newVersion())
                            : QStringLiteral("Nova versão %1 disponível.").arg(newVersion()));
        if (m_startupBusy && installed()) downloadAndInstall();
        else {
            m_startupBusy = false;
            setStatus(installed() ? QStringLiteral("Nova versão %1 disponível. Será instalada na próxima abertura.").arg(newVersion())
                : QStringLiteral("Nova versão %1 disponível. Na versão portátil, use a página de downloads.").arg(newVersion()));
        }
}
void AutoUpdater::downloadUpdate() {
    if (m_busy || m_offer.isEmpty() || !installed()) return;
    const QString file = cachePath() + QLatin1Char('/') + m_offer.value("filename").toString();
    const QString digest = m_offer.value("sha256").toString();
    const QString newVersion = m_offer.value("version").toString();
    if (verifyInstallerDigest(file, digest)) {
        m_readyFile = file; setStatus(QStringLiteral("Versão %1 pronta. Use Aplicar e reiniciar para instalar.").arg(newVersion));
        if (m_installAfterDownload) installNow();
        return;
    }
    if (!QDir().mkpath(cachePath())) { fail(QStringLiteral("Não foi possível criar a pasta de atualização.")); return; }
    auto output = std::make_shared<QSaveFile>(file);
    if (!output->open(QIODevice::WriteOnly)) { fail(QStringLiteral("Não foi possível salvar o instalador.")); return; }
    const auto offer = m_offer;
    const int generation = m_generation;
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto count = std::make_shared<qint64>(0);
    auto *reply = m_network.get(requestFor(QUrl(offer.value("url").toString()))); m_reply = reply;
    setStatus(QStringLiteral("Baixando OpenSpeak %1 em segundo plano…").arg(newVersion), true);
    const auto read = [reply, output, hash, count, offer] {
        const auto bytes = reply->readAll(); *count += bytes.size();
        if (*count > qint64(offer.value("size").toDouble()) || output->write(bytes) != bytes.size()) { reply->abort(); return; }
        hash->addData(bytes);
    };
    connect(reply, &QNetworkReply::readyRead, this, read);
    connect(reply, &QNetworkReply::finished, this, [this, reply, output, hash, count, offer, generation, file, read] {
        reply->deleteLater();
        if (generation != m_generation) { output->cancelWriting(); return; }
        read();
        if (reply->error() != QNetworkReply::NoError || *count != qint64(offer.value("size").toDouble())
            || QString::fromLatin1(hash->result().toHex()) != offer.value("sha256").toString() || !output->commit()) {
            output->cancelWriting(); fail(QStringLiteral("Download não validado. Nenhuma atualização será instalada.")); return;
        }
        m_readyFile = file;
        setStatus(QStringLiteral("Versão %1 pronta. Use Aplicar e reiniciar para instalar.").arg(offer.value("version").toString()));
        if (m_installAfterDownload) installNow();
    });
}
void AutoUpdater::downloadAndInstall() {
    if (m_busy || !available() || !installed()) return;
    m_installAfterDownload = true;
    if (ready()) installNow();
    else downloadUpdate();
}
void AutoUpdater::installNow() {
    if (!ready()) return;
    m_manualInstall = true;
    setStatus(QStringLiteral("Instalando atualização e reiniciando o OpenSpeak…"), true);
    QCoreApplication::quit();
}
void AutoUpdater::installOnExit() {
    if (!installed() || !ready() || !m_manualInstall) return;
    if (m_offer.value("prerelease").toBool() && !m_settings->prereleaseUpdates()) return;
    QStringList args{QStringLiteral("--apply-update"), m_readyFile, QStringLiteral("--update-sha256"), m_offer.value("sha256").toString(),
                     QStringLiteral("--wait-pid"), QString::number(QCoreApplication::applicationPid())};
    if (m_manualInstall) args.append(QStringLiteral("--restart-after-update"));
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
}
int AutoUpdater::runInstallHelper(const QString &file, const QString &sha256, quint32 parentPid, bool restart) {
#ifdef Q_OS_WIN
    const QFileInfo info(file);
    if (info.absolutePath() != QDir(cachePath()).absolutePath() || !info.fileName().startsWith(QLatin1String("OpenSpeak-"))
        || !info.fileName().endsWith(QLatin1String(".exe")) || !verifyInstallerDigest(file, sha256)) return 1;
    if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parentPid)) {
        const auto result = WaitForSingleObject(process, 90'000); CloseHandle(process);
        if (result != WAIT_OBJECT_0) return 1;
    }
    if (!verifyInstallerDigest(file, sha256)) return 1;
    // Não encerra outras instâncias nem reinicia o Windows. A elevação fica a cargo do UAC.
    // /SILENT mostra só a barra de progresso: o app fecha e a instalação fica visível.
    const QString args = QStringLiteral("/SILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /UPDATE=1 /RELAUNCH=%1").arg(restart ? 1 : 0);
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(file.utf16()),
        reinterpret_cast<LPCWSTR>(args.utf16()), nullptr, SW_HIDE));
    return result > 32 ? 0 : 1;
#else
    Q_UNUSED(file); Q_UNUSED(sha256); Q_UNUSED(parentPid); Q_UNUSED(restart);
    return 1;
#endif
}
}

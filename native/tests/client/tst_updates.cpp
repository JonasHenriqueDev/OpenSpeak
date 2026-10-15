#include "UpdatePolicy.h"
#include "AutoUpdater.h"
#include <QTest>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
using namespace openspeak;
namespace openspeak {
class TestUpdaterStartup : public QObject {
    Q_OBJECT
    static QJsonObject release(QString tag, bool pre = false, QString digest = QStringLiteral("sha256:") + QString(64, 'a'),
                               QString prefix = QStringLiteral("OpenSpeak-Setup-")) {
        const auto version = tag.mid(1);
        const QString name = prefix + version + ".exe";
        return {{"tag_name", tag}, {"prerelease", pre}, {"draft", false}, {"assets", QJsonArray{QJsonObject{
            {"name", name}, {"size", 123}, {"state", "uploaded"}, {"digest", digest},
            {"browser_download_url", "https://github.com/JonasHenriqueDev/OpenSpeak/releases/download/" + tag + "/" + name}}}}};
    }
private slots:
    void fallbackWithoutApiUsesTagRedirectAndChecksums() {
        QCOMPARE(releaseTagFromUrl(QUrl("https://github.com/JonasHenriqueDev/OpenSpeak/releases/tag/v1.0.9")), QStringLiteral("v1.0.9"));
        QVERIFY(releaseTagFromUrl(QUrl("https://github.com/JonasHenriqueDev/OpenSpeak/releases")).isEmpty());
        QVERIFY(releaseTagFromUrl(QUrl("https://evil.example/JonasHenriqueDev/OpenSpeak/releases/tag/v1.0.9")).isEmpty());
        QVERIFY(releaseTagFromUrl(QUrl("https://github.com/Outro/OpenSpeak/releases/tag/v1.0.9")).isEmpty());
        const QByteArray sums = QByteArray(64, 'b') + " *OpenSpeak-Setup-1.0.9.exe\r\n"
                              + QByteArray(64, 'C') + " *OpenSpeak-Full-Setup-1.0.9.exe\n";
        const auto lite = releaseFromChecksums("v1.0.9", sums, "OpenSpeak-Setup-1.0.9.exe", 1234);
        const auto offer = selectInstallerRelease(QJsonArray{lite}, false, "lite");
        QCOMPARE(offer.value("version").toString(), QStringLiteral("1.0.9"));
        QCOMPARE(offer.value("sha256").toString(), QString(64, 'b'));
        QCOMPARE(offer.value("size").toDouble(), 1234.0);
        const auto full = releaseFromChecksums("v1.0.9", sums, "OpenSpeak-Full-Setup-1.0.9.exe", 99);
        QCOMPARE(selectInstallerRelease(QJsonArray{full}, false, "full").value("sha256").toString(), QString(64, 'c'));
        QVERIFY(releaseFromChecksums("v1.0.9", sums, "OpenSpeak-Setup-2.0.0.exe", 1).isEmpty());
    }
    void installsVerifiedStartupUpdateWithoutPromptEvenWhenPeriodicChecksAreOff() {
        QStandardPaths::setTestModeEnabled(true);
        Settings settings(QStringLiteral("update-startup-test"), true);
        settings.setAutomaticUpdates(false);
        settings.setPrereleaseUpdates(false);
        settings.setUpdateEdition(QString());
        class InstalledUpdater : public AutoUpdater {
        public:
            using AutoUpdater::AutoUpdater;
            bool installed() const override { return true; }
            void installNow() override { ++installations; }
            int installations = 0;
        };
        const QByteArray bytes(123, 'v');
        const QString digest = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
        QVERIFY(QDir().mkpath(directory));
        const QString cached = directory + "/OpenSpeak-Setup-99.0.0.exe";
        const auto cleanup = qScopeGuard([&] { QFile::remove(cached); });
        QFile file(cached);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size())); file.close();
        InstalledUpdater startup(&settings);
        startup.m_edition = QStringLiteral("lite");
        QVERIFY(startup.startupBusy());
        startup.handleReleaseDocument(QJsonDocument(release("v99.0.0", false, digest)));
        QCOMPARE(startup.installations, 1);
        QVERIFY(startup.ready());
        QCOMPARE(startup.m_readyFile, cached);
        QVERIFY(startup.m_reply.isNull()); // Validated cache, no redundant download.
        QVERIFY(startup.startupBusy()); // Login stays blocked until restart.
        InstalledUpdater duringUse(&settings);
        duringUse.m_edition = QStringLiteral("lite");
        duringUse.m_startupBusy = false;
        duringUse.handleReleaseDocument(QJsonDocument(release("v99.0.0", false, digest)));
        QCOMPARE(duringUse.installations, 0); // Never interrupt an active session.
        QVERIFY(!duringUse.ready());
        QVERIFY(duringUse.available());
    }
    void releasesLoginWhenUpToDateOrStartupFails() {
        Settings settings(QStringLiteral("update-startup-test"), true);
        settings.setAutomaticUpdates(false);
        settings.setPrereleaseUpdates(false);
        settings.setUpdateEdition(QString());
        AutoUpdater updater(&settings);
        updater.m_edition = QStringLiteral("lite");
        updater.handleReleaseDocument(QJsonDocument(release("v" + updater.version())));
        QVERIFY(!updater.available());
        QVERIFY(!updater.startupBusy());
        AutoUpdater failed(&settings);
        failed.fail(QStringLiteral("Network unavailable"));
        QVERIFY(!failed.startupBusy());
        QVERIFY(!failed.busy());
        QVERIFY(!failed.m_installAfterDownload);
        AutoUpdater portable(&settings);
        portable.m_edition = QStringLiteral("lite");
        portable.handleReleaseDocument(QJsonDocument(QJsonArray{release("v100.0.0-beta.1", true), release("v99.0.0")}));
        QCOMPARE(portable.newVersion(), QStringLiteral("99.0.0"));
        QVERIFY(!portable.startupBusy());
        QVERIFY(!portable.m_manualInstall);
    }
    void editionsUseTheirOwnInstallers() {
        const QString full = QStringLiteral("OpenSpeak-Full-Setup-");
        const QString digest = QStringLiteral("sha256:") + QString(64, 'a');
        auto both = release("v1.2.0");
        auto assets = both.value("assets").toArray();
        assets.append(release("v1.2.0", false, digest, full).value("assets").toArray().at(0));
        both["assets"] = assets;
        QCOMPARE(selectInstallerRelease(QJsonArray{both}, false, "lite").value("filename").toString(), QStringLiteral("OpenSpeak-Setup-1.2.0.exe"));
        QCOMPARE(selectInstallerRelease(QJsonArray{both}, false, "full").value("filename").toString(), QStringLiteral("OpenSpeak-Full-Setup-1.2.0.exe"));
        QVERIFY(selectInstallerRelease(QJsonArray{release("v1.2.0")}, false, "full").isEmpty());
        QCOMPARE(normalizeEdition(QStringLiteral(" FULL ")), QStringLiteral("full"));
        QCOMPARE(normalizeEdition("qualquer"), QStringLiteral("lite"));

        Settings settings(QStringLiteral("update-edition-test"), true);
        settings.setPrereleaseUpdates(false);
        settings.setUpdateEdition(QStringLiteral("full"));
        const auto cleanup = qScopeGuard([&] { settings.setUpdateEdition(QString()); });
        AutoUpdater updater(&settings);
        updater.m_startupBusy = false;
        updater.m_edition = QStringLiteral("lite");
        QCOMPARE(updater.targetEdition(), QStringLiteral("full"));
        // Mesma versão na outra edição: reinstala para trocar de canal.
        updater.handleReleaseDocument(QJsonDocument(release("v" + updater.version(), false, digest, full)));
        QVERIFY(updater.available());
        // Nunca faz downgrade ao trocar de edição.
        updater.handleReleaseDocument(QJsonDocument(release("v0.0.1", false, digest, full)));
        QVERIFY(!updater.available());
        // Sem troca, a mesma versão está atualizada.
        updater.m_edition = QStringLiteral("full");
        updater.handleReleaseDocument(QJsonDocument(release("v" + updater.version(), false, digest, full)));
        QVERIFY(!updater.available());
    }
    void corruptedDownloadsAreRejected() {
        QTemporaryDir directory;
        QFile file(directory.filePath("setup.exe"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("verified release bytes"); file.close();
        const QString digest = QString::fromLatin1(QCryptographicHash::hash("verified release bytes", QCryptographicHash::Sha256).toHex());
        QVERIFY(verifyInstallerDigest(file.fileName(), digest));
        QVERIFY(file.open(QIODevice::Append)); file.write("corruption"); file.close();
        QVERIFY(!verifyInstallerDigest(file.fileName(), digest));
        QVERIFY(!verifyInstallerDigest(directory.filePath("missing.exe"), digest));
    }
    void semanticOrder() {
        QVERIFY(compareReleaseVersions("1.0.0", "0.2.7") > 0);
        QVERIFY(compareReleaseVersions("1.0.10", "1.0.9") > 0);
        QVERIFY(compareReleaseVersions("1.1.0-beta.10", "1.1.0-beta.2") > 0);
        QVERIFY(compareReleaseVersions("1.1.0", "1.1.0-rc.1") > 0);
        QCOMPARE(compareReleaseVersions("v1.0.0", "1.0.0+build.1"), 0);
        QVERIFY(!validReleaseVersion("../malicious"));
    }
    void stableAndOptInChannels() {
        const QJsonArray releases{release("v1.1.0-beta.1", true), release("v1.0.0"), release("v0.2.7", true)};
        QCOMPARE(selectInstallerRelease(releases, false).value("version").toString(), QStringLiteral("1.0.0"));
        QCOMPARE(selectInstallerRelease(releases, true).value("version").toString(), QStringLiteral("1.1.0-beta.1"));
        auto draft = release("v2.0.0"); draft["draft"] = true;
        QCOMPARE(selectInstallerRelease(QJsonArray{draft, release("v1.0.0")}, true).value("version").toString(), QStringLiteral("1.0.0"));
        QVERIFY(selectInstallerRelease(QJsonArray{release("v1.1.0-beta.1", false)}, false).isEmpty());
    }
    void rejectsUnverifiedAndForeignInstallers() {
        QVERIFY(selectInstallerRelease(QJsonArray{release("v1.0.0", false, "")}, false).isEmpty());
        auto foreign = release("v1.0.0"); auto assets = foreign.value("assets").toArray(); auto asset = assets[0].toObject();
        asset["browser_download_url"] = "https://example.com/setup.exe"; assets[0] = asset; foreign["assets"] = assets;
        QVERIFY(selectInstallerRelease(QJsonArray{foreign}, false).isEmpty());
        asset["browser_download_url"] = "https://github.com/Other/Repository/releases/download/v1.0.0/setup.exe";
        assets[0] = asset; foreign["assets"] = assets;
        QVERIFY(selectInstallerRelease(QJsonArray{foreign}, true).isEmpty());
    }
};
}
QTEST_GUILESS_MAIN(openspeak::TestUpdaterStartup)
#include "tst_updates.moc"

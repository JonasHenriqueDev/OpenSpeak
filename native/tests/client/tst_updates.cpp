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
    static QJsonObject release(QString tag, bool pre = false, QString digest = QStringLiteral("sha256:") + QString(64, 'a')) {
        const auto version = tag.mid(1);
        const QString name = "OpenSpeak-Setup-" + version + ".exe";
        return {{"tag_name", tag}, {"prerelease", pre}, {"draft", false}, {"assets", QJsonArray{QJsonObject{
            {"name", name}, {"size", 123}, {"state", "uploaded"}, {"digest", digest},
            {"browser_download_url", "https://github.com/JonasHenriqueDev/OpenSpeak/releases/download/" + tag + "/" + name}}}}};
    }
private slots:
    void installsVerifiedStartupUpdateWithoutPromptEvenWhenPeriodicChecksAreOff() {
        QStandardPaths::setTestModeEnabled(true);
        Settings settings(QStringLiteral("update-startup-test"), true);
        settings.setAutomaticUpdates(false);
        settings.setPrereleaseUpdates(false);
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
        QVERIFY(startup.startupBusy());
        startup.handleReleaseDocument(QJsonDocument(release("v99.0.0", false, digest)));
        QCOMPARE(startup.installations, 1);
        QVERIFY(startup.ready());
        QCOMPARE(startup.m_readyFile, cached);
        QVERIFY(startup.m_reply.isNull()); // Validated cache, no redundant download.
        QVERIFY(startup.startupBusy()); // Login stays blocked until restart.
        InstalledUpdater duringUse(&settings);
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
        AutoUpdater updater(&settings);
        updater.handleReleaseDocument(QJsonDocument(release("v" + updater.version())));
        QVERIFY(!updater.available());
        QVERIFY(!updater.startupBusy());
        AutoUpdater failed(&settings);
        failed.fail(QStringLiteral("Network unavailable"));
        QVERIFY(!failed.startupBusy());
        QVERIFY(!failed.busy());
        QVERIFY(!failed.m_installAfterDownload);
        AutoUpdater portable(&settings);
        portable.handleReleaseDocument(QJsonDocument(QJsonArray{release("v100.0.0-beta.1", true), release("v99.0.0")}));
        QCOMPARE(portable.newVersion(), QStringLiteral("99.0.0"));
        QVERIFY(!portable.startupBusy());
        QVERIFY(!portable.m_manualInstall);
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

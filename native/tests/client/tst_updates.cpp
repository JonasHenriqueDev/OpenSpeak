#include "UpdatePolicy.h"
#include "AutoUpdater.h"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
using namespace openspeak;
namespace openspeak {
class TestUpdaterConsent : public QObject {
    Q_OBJECT
    static QJsonObject release(QString tag, bool pre = false, QString digest = QStringLiteral("sha256:") + QString(64, 'a')) {
        const auto version = tag.mid(1);
        const QString name = "OpenSpeak-Setup-" + version + ".exe";
        return {{"tag_name", tag}, {"prerelease", pre}, {"draft", false}, {"assets", QJsonArray{QJsonObject{
            {"name", name}, {"size", 123}, {"state", "uploaded"}, {"digest", digest},
            {"browser_download_url", "https://github.com/JonasHenriqueDev/OpenSpeak/releases/download/" + tag + "/" + name}}}}};
    }
private slots:
    void asksOncePerLaunchWithoutDownloadingOrInstalling() {
        Settings settings(QStringLiteral("update-consent-test"), true);
        settings.setAutomaticUpdates(true);
        settings.setPrereleaseUpdates(false);
        const QJsonDocument document(release("v99.0.0"));
        AutoUpdater updater(&settings);
        QSignalSpy prompt(&updater, &AutoUpdater::updateAvailable);
        updater.handleReleaseDocument(document);
        QCOMPARE(prompt.count(), 1);
        QCOMPARE(updater.newVersion(), QStringLiteral("99.0.0"));
        QVERIFY(updater.available());
        QVERIFY(!updater.busy());
        QVERIFY(!updater.ready());
        QVERIFY(updater.m_reply.isNull());
        QVERIFY(!updater.m_manualInstall);
        updater.handleReleaseDocument(document);
        QCOMPARE(prompt.count(), 1); // "Agora não" is respected during this launch.
        AutoUpdater reopened(&settings);
        QSignalSpy reopenedPrompt(&reopened, &AutoUpdater::updateAvailable);
        reopened.handleReleaseDocument(document);
        QCOMPARE(reopenedPrompt.count(), 1);
    }
    void checksStartupOfferEvenWithoutPeriodicChecks() {
        Settings settings(QStringLiteral("update-startup-test"), true);
        settings.setAutomaticUpdates(false);
        settings.setPrereleaseUpdates(false);
        AutoUpdater updater(&settings);
        QSignalSpy prompt(&updater, &AutoUpdater::updateAvailable);
        updater.handleReleaseDocument(QJsonDocument(release("v" + updater.version())));
        QCOMPARE(prompt.count(), 0);
        QVERIFY(!updater.available());
        updater.handleReleaseDocument(QJsonDocument(QJsonArray{release("v100.0.0-beta.1", true), release("v99.0.0")}));
        QCOMPARE(prompt.count(), 1);
        QCOMPARE(updater.newVersion(), QStringLiteral("99.0.0"));
        QVERIFY(!updater.m_manualInstall);
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
QTEST_GUILESS_MAIN(openspeak::TestUpdaterConsent)
#include "tst_updates.moc"

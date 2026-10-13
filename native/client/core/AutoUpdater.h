#pragma once
#include "Settings.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

namespace openspeak {
class AutoUpdater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool installed READ installed CONSTANT)
    Q_PROPERTY(QString newVersion READ newVersion NOTIFY changed)
public:
    explicit AutoUpdater(Settings *settings, QObject *parent = nullptr);
    QString version() const;
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool ready() const { return !m_readyFile.isEmpty(); }
    bool available() const { return !m_offer.isEmpty(); }
    bool installed() const;
    QString newVersion() const { return m_offer.value("version").toString(); }
    Q_INVOKABLE void check();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void installNow();
    void installOnExit();
    static int runInstallHelper(const QString &file, const QString &sha256, quint32 parentPid, bool restart);
signals:
    void changed();
    void updateAvailable();
private:
    friend class TestUpdaterConsent;
    void handleReleaseDocument(const QJsonDocument &doc);
    void setStatus(const QString &status, bool busy = false);
    void settingsChanged();
    Settings *m_settings;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QJsonObject m_offer;
    QString m_status = QStringLiteral("Verificando atualizações em breve…");
    QString m_readyFile;
    QString m_promptedVersion;
    bool m_installAfterDownload = false;
    bool m_busy = false, m_manualInstall = false;
    bool m_previousAuto, m_previousPre;
    int m_generation = 0;
};
}

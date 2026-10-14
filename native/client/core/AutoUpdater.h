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
    Q_PROPERTY(bool startupBusy READ startupBusy NOTIFY changed)
    Q_PROPERTY(QString edition READ edition CONSTANT)
    Q_PROPERTY(QString targetEdition READ targetEdition NOTIFY changed)
public:
    explicit AutoUpdater(Settings *settings, QObject *parent = nullptr);
    QString version() const;
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool ready() const { return !m_readyFile.isEmpty(); }
    bool available() const { return !m_offer.isEmpty(); }
    virtual bool installed() const;
    bool startupBusy() const { return m_startupBusy; }
    QString newVersion() const { return m_offer.value("version").toString(); }
    /// Edição desta instalação, lida de edition.txt ao lado do .exe (sem o arquivo: "full").
    static QString installedEdition();
    QString edition() const { return m_edition; }
    QString targetEdition() const;
    Q_INVOKABLE void check();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE virtual void installNow();
    void installOnExit();
    static int runInstallHelper(const QString &file, const QString &sha256, quint32 parentPid, bool restart);
signals:
    void changed();
private:
    friend class TestUpdaterStartup;
    void handleReleaseDocument(const QJsonDocument &doc);
    void setStatus(const QString &status, bool busy = false);
    void fail(const QString &status);
    void settingsChanged();
    Settings *m_settings;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QJsonObject m_offer;
    QString m_status = QStringLiteral("Verificando atualizações em breve…");
    QString m_readyFile;
    bool m_startupBusy = true;
    bool m_installAfterDownload = false;
    bool m_busy = false, m_manualInstall = false;
    bool m_previousAuto, m_previousPre;
    QString m_previousEdition;
    QString m_edition = installedEdition();
    int m_generation = 0;
};
}

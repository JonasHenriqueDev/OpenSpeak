#pragma once
#include "Settings.h"
#include <QJsonObject>
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
public:
    explicit AutoUpdater(Settings *settings, QObject *parent = nullptr);
    QString version() const;
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool ready() const { return !m_readyFile.isEmpty(); }
    bool available() const { return !m_offer.isEmpty(); }
    bool installed() const;
    Q_INVOKABLE void check();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void installNow();
    void installOnExit();
    static int runInstallHelper(const QString &file, const QString &sha256, quint32 parentPid, bool restart);
signals:
    void changed();
private:
    void setStatus(const QString &status, bool busy = false);
    void settingsChanged();
    Settings *m_settings;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QJsonObject m_offer;
    QString m_status = QStringLiteral("Verificando atualizações em breve…");
    QString m_readyFile;
    bool m_busy = false, m_manualInstall = false;
    bool m_previousAuto, m_previousPre;
    int m_generation = 0;
};
}

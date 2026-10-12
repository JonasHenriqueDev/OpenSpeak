#pragma once
// Preferências locais (QSettings). `--profile nome` separa identidades para
// rodar duas instâncias na mesma máquina.

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

namespace openspeak {

class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString nickname READ nickname WRITE setNickname NOTIFY changed)
    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY changed)
    Q_PROPERTY(QString uid READ uid CONSTANT)
    Q_PROPERTY(QString preferredEncoder READ preferredEncoder WRITE setPreferredEncoder NOTIFY changed)
    Q_PROPERTY(bool showOwnStreams READ showOwnStreams WRITE setShowOwnStreams NOTIFY changed)
    Q_PROPERTY(bool showInactiveStreams READ showInactiveStreams WRITE setShowInactiveStreams NOTIFY changed)
    Q_PROPERTY(bool advancedMode READ advancedMode WRITE setAdvancedMode NOTIFY changed)
    Q_PROPERTY(QStringList hiddenStreams READ hiddenStreams WRITE setHiddenStreams NOTIFY changed)
    Q_PROPERTY(QStringList contacts READ contacts NOTIFY changed)
    Q_PROPERTY(QVariantMap lastStreamingConfig READ lastStreamingConfig WRITE setLastStreamingConfig NOTIFY changed)
    Q_PROPERTY(bool debug READ debug CONSTANT)
    Q_PROPERTY(QString connectionMode READ connectionMode WRITE setConnectionMode NOTIFY changed)
public:
    explicit Settings(const QString &profile, bool debug, QObject *parent = nullptr);

    QString nickname() const { return value("nickname").toString(); }
    void setNickname(const QString &v) { set("nickname", v); }
    QString serverUrl() const { return value("serverUrl", QStringLiteral("ws://localhost:9988/ws")).toString(); }
    void setServerUrl(const QString &v) { set("serverUrl", v); }
    QString uid() const { return m_uid; }
    QString preferredEncoder() const { return value("preferredEncoder").toString(); }
    void setPreferredEncoder(const QString &v) { set("preferredEncoder", v); }
    bool showOwnStreams() const { return value("showOwnStreams", true).toBool(); }
    void setShowOwnStreams(bool v) { set("showOwnStreams", v); }
    bool showInactiveStreams() const { return value("showInactiveStreams", true).toBool(); }
    void setShowInactiveStreams(bool v) { set("showInactiveStreams", v); }
    bool advancedMode() const { return value("advancedMode", false).toBool(); }
    void setAdvancedMode(bool v) { set("advancedMode", v); }
    QStringList hiddenStreams() const { return value("hiddenStreams").toStringList(); }
    void setHiddenStreams(const QStringList &v) { set("hiddenStreams", v); }
    QStringList contacts() const { return value("contacts").toStringList(); }
    QVariantMap lastStreamingConfig() const;
    void setLastStreamingConfig(const QVariantMap &v) { set("lastStreamingConfig", v); }
    bool debug() const { return m_debug; }
    /// "tailnet" (padrão, sem servidor) ou "server" (servidor dedicado).
    QString connectionMode() const { return value("connectionMode", QStringLiteral("tailnet")).toString(); }
    void setConnectionMode(const QString &v) { set("connectionMode", v); }

    Q_INVOKABLE bool isContact(const QString &uid) const { return contacts().contains(uid); }
    Q_INVOKABLE void toggleContact(const QString &uid);
    Q_INVOKABLE void hideStream(const QString &id);
    Q_INVOKABLE void showStream(const QString &id);

signals:
    void changed();

private:
    QVariant value(const QString &key, const QVariant &def = {}) const;
    void set(const QString &key, const QVariant &v);

    QSettings m_settings;
    QString m_group;
    QString m_uid;
    bool m_debug = false;
};

} // namespace openspeak

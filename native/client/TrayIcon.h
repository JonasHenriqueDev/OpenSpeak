#pragma once
// Ícone na bandeja do sistema (como no Discord). O QML decide o que cada ação
// faz; aqui ficam só o ícone, o menu de contexto e os balões de notificação.

#include <QObject>
#include <QVariantMap>

class QAction;
class QMenu;
class QSystemTrayIcon;

class TrayIcon : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY changed)
    /// Estado refletido no menu (transmitindo / áudio silenciado).
    Q_PROPERTY(bool streaming READ streaming WRITE setStreaming NOTIFY changed)
    Q_PROPERTY(bool audioMuted READ audioMuted WRITE setAudioMuted NOTIFY changed)
    Q_PROPERTY(bool soundsMuted READ soundsMuted WRITE setSoundsMuted NOTIFY changed)
    /// Textos do menu vindos do I18n.js: open, stop, mute, muteSounds, settings, quit, tooltip, tooltipStreaming.
    Q_PROPERTY(QVariantMap texts READ texts WRITE setTexts NOTIFY changed)
public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    bool available() const;
    bool visible() const;
    void setVisible(bool v);
    bool streaming() const { return m_streaming; }
    void setStreaming(bool v);
    bool audioMuted() const { return m_audioMuted; }
    void setAudioMuted(bool v);
    bool soundsMuted() const { return m_soundsMuted; }
    void setSoundsMuted(bool v);
    QVariantMap texts() const { return m_texts; }
    void setTexts(const QVariantMap &texts);

    Q_INVOKABLE void showMessage(const QString &title, const QString &text);

signals:
    void changed();
    void openRequested();
    void stopStreamingRequested();
    void toggleMuteRequested();
    void toggleSoundsRequested();
    void settingsRequested();
    void quitRequested();
    /// Clique no balão de notificação.
    void messageClicked();

private:
    void refresh();
    QString text(const char *key, const QString &fallback) const;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_open = nullptr;
    QAction *m_stop = nullptr;
    QAction *m_mute = nullptr;
    QAction *m_muteSounds = nullptr;
    QAction *m_settings = nullptr;
    QAction *m_quit = nullptr;
    bool m_streaming = false;
    bool m_audioMuted = false;
    bool m_soundsMuted = false;
    QVariantMap m_texts;
};

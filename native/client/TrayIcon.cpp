#include "TrayIcon.h"

#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>

TrayIcon::TrayIcon(QObject *parent) : QObject(parent)
{
    m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
    m_menu = new QMenu;
    m_open = m_menu->addAction(QString());
    QFont bold = m_open->font();
    bold.setBold(true);
    m_open->setFont(bold);
    m_menu->addSeparator();
    m_stop = m_menu->addAction(QString());
    m_mute = m_menu->addAction(QString());
    m_mute->setCheckable(true);
    m_menu->addSeparator();
    m_settings = m_menu->addAction(QString());
    m_menu->addSeparator();
    m_quit = m_menu->addAction(QString());
    m_tray->setContextMenu(m_menu);

    connect(m_open, &QAction::triggered, this, &TrayIcon::openRequested);
    connect(m_stop, &QAction::triggered, this, &TrayIcon::stopStreamingRequested);
    connect(m_mute, &QAction::triggered, this, &TrayIcon::toggleMuteRequested);
    connect(m_settings, &QAction::triggered, this, &TrayIcon::settingsRequested);
    connect(m_quit, &QAction::triggered, this, &TrayIcon::quitRequested);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayIcon::messageClicked);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        // Clique esquerdo (ou duplo) mostra a janela; o direito abre o menu sozinho.
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    refresh();
}

TrayIcon::~TrayIcon()
{
    m_tray->hide();
    delete m_menu;
}

bool TrayIcon::available() const { return QSystemTrayIcon::isSystemTrayAvailable(); }

bool TrayIcon::visible() const { return m_tray->isVisible(); }

void TrayIcon::setVisible(bool v)
{
    if (v == m_tray->isVisible())
        return;
    m_tray->setVisible(v);
    emit changed();
}

void TrayIcon::setStreaming(bool v)
{
    if (v == m_streaming)
        return;
    m_streaming = v;
    refresh();
    emit changed();
}

void TrayIcon::setAudioMuted(bool v)
{
    if (v == m_audioMuted)
        return;
    m_audioMuted = v;
    refresh();
    emit changed();
}

void TrayIcon::setTexts(const QVariantMap &texts)
{
    m_texts = texts;
    refresh();
    emit changed();
}

void TrayIcon::showMessage(const QString &title, const QString &text)
{
    if (m_tray->isVisible() && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, text, QApplication::windowIcon(), 6000);
}

QString TrayIcon::text(const char *key, const QString &fallback) const
{
    const QString t = m_texts.value(QLatin1String(key)).toString();
    return t.isEmpty() ? fallback : t;
}

void TrayIcon::refresh()
{
    m_open->setText(text("open", QStringLiteral("Abrir OpenSpeak")));
    m_stop->setText(text("stop", QStringLiteral("Parar transmissão")));
    m_stop->setVisible(m_streaming);
    m_mute->setText(text("mute", QStringLiteral("Silenciar streams")));
    m_mute->setChecked(m_audioMuted);
    m_settings->setText(text("settings", QStringLiteral("Preferências…")));
    m_quit->setText(text("quit", QStringLiteral("Sair do OpenSpeak")));
    m_tray->setToolTip(m_streaming ? text("tooltipStreaming", QStringLiteral("OpenSpeak — transmitindo"))
                                   : text("tooltip", QStringLiteral("OpenSpeak")));
}

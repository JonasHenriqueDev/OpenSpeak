#pragma once
// Ajustes de janela que dependem da plataforma (Windows), chamados pelo QML.

#include <QAbstractNativeEventFilter>
#include <QColor>
#include <QList>
#include <QObject>
#include <QPointer>

class QWindow;

class NativeWindow : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    using QObject::QObject;
    ~NativeWindow() override;

    /// Em tela cheia sem borda o Windows trata a janela como "app em tela cheia" e
    /// não mostra a barra de tarefas oculta automaticamente ao passar o mouse.
    /// Com a borda de 1 px (WS_BORDER) ela volta a aparecer.
    Q_INVOKABLE void setTaskbarInFullScreen(QWindow *window, bool enabled);

    /// Janela sem moldura não tem o Aero Snap: ao fim de um arrasto (startSystemMove)
    /// com o cursor no topo do monitor, maximiza como as janelas do Windows.
    Q_INVOKABLE void enableSnapToTop(QWindow *window);

    /// Cor dos links em Markdown/RichText: o QTextDocument usa a paleta da
    /// aplicação e ignora `linkColor`/`palette` do Label.
    Q_INVOKABLE void setLinkColor(const QColor &color);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QList<QPointer<QWindow>> m_snapWindows;
    bool m_filterInstalled = false;
    bool m_sizing = false; // o laço modal atual é de redimensionar, não de mover
};

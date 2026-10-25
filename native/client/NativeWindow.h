#pragma once
// Ajustes de janela que dependem da plataforma (Windows), chamados pelo QML.

#include <QObject>

class QWindow;

class NativeWindow : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    /// Em tela cheia sem borda o Windows trata a janela como "app em tela cheia" e
    /// não mostra a barra de tarefas oculta automaticamente ao passar o mouse.
    /// Com a borda de 1 px (WS_BORDER) ela volta a aparecer.
    Q_INVOKABLE void setTaskbarInFullScreen(QWindow *window, bool enabled);
};

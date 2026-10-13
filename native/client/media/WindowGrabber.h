#pragma once
// Captura de janela sem a borda amarela do Windows.Graphics.Capture (Windows 10 não
// permite desligá-la). Híbrido, como o CroppingWindowCapturer do WebRTC:
//  A) janela sem nada por cima: duplicação da tela (DXGI) recortada na GPU;
//  B) janela coberta: PrintWindow(PW_RENDERFULLCONTENT);
//  C) preto nos dois por ~1 s (alguns jogos/D3D): emite fallbackRequested e o
//     VideoSource volta para o QWindowCapture (com borda).

#include <QObject>
#include <QThread>
#include <QVideoFrame>

#include <memory>

class QTimer;

namespace openspeak {

class WindowGrabber : public QObject {
    Q_OBJECT
public:
    WindowGrabber(quintptr hwnd, int targetFps, QObject *parent = nullptr);
    ~WindowGrabber() override;

    void start();
    void stop();

signals:
    void frameReady(const QVideoFrame &frame);
    /// Método em uso, para as informações da transmissão ("Duplicação de tela (DXGI)"...).
    void methodChanged(const QString &method);
    void windowClosed();
    void fallbackRequested();

private:
    struct Impl;
    void tick();

    QThread m_thread;
    QObject *m_context = nullptr; // vive na m_thread
    QTimer *m_timer = nullptr;    // idem
    std::unique_ptr<Impl> m_impl; // usado só na m_thread
    int m_fps;
};

} // namespace openspeak

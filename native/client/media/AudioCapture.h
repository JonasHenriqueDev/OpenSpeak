#pragma once
// Captura do áudio que acompanha o stream.
//  - Tela:   todo o áudio do sistema EXCETO o próprio OpenSpeak (evita eco)
//  - Janela: apenas o áudio do processo dono da janela
//  - Câmera: microfone padrão
// Usa o "process loopback" do WASAPI (Windows 10 2004+), como o
// ProcessAudioCapturer do TeamSpeak. Saída: float32 intercalado.

#include <QAudioSource>
#include <QByteArray>
#include <QIODevice>
#include <QObject>

#include <atomic>
#include <memory>
#include <thread>

namespace openspeak {

class AudioCapture : public QObject {
    Q_OBJECT
public:
    enum class Mode { SystemExcludingSelf, Process, Microphone };

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    /// `pid` só é usado em Mode::Process.
    bool start(Mode mode, quint32 pid = 0);
    void stop();
    bool isRunning() const { return m_running; }

    static constexpr int SampleRate = 48000;
    static constexpr int Channels = 2;

    /// Procura o PID do processo dono de uma janela pelo título.
    static quint32 pidForWindowTitle(const QString &title);

signals:
    /// Amostras float32 intercaladas (SampleRate/Channels acima).
    void samplesAvailable(const QByteArray &pcm, int sampleRate, int channels);
    void errorOccurred(const QString &error);

private:
    void loopbackThread(Mode mode, quint32 pid);

    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::unique_ptr<QAudioSource> m_mic;
    QIODevice *m_micDevice = nullptr;
};

} // namespace openspeak

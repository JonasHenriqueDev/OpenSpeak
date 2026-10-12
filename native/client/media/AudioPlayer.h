#pragma once
// Reprodução do áudio de um stream assistido (float32 48 kHz estéreo).
// Mantém a latência baixa descartando o excesso acumulado no buffer.

#include <QAudioSink>
#include <QIODevice>
#include <QObject>

#include <memory>

namespace openspeak {

class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    void write(const QByteArray &pcm);
    void setVolume(double volume);
    double volume() const { return m_volume; }

private:
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice *m_device = nullptr;
    double m_volume = 1.0;
    qsizetype m_maxBuffered = 0;
};

} // namespace openspeak

#include "AudioPlayer.h"

#include <QMediaDevices>

namespace openspeak {

AudioPlayer::AudioPlayer(QObject *parent) : QObject(parent)
{
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return;
    m_sink = std::make_unique<QAudioSink>(device, fmt);
    m_sink->setBufferSize(48000 * 2 * 4 / 5); // 200 ms
    m_device = m_sink->start();
    // Mais que ~150 ms acumulados = atraso perceptível: descarta.
    m_maxBuffered = 48000 * 2 * 4 * 150 / 1000;
}

AudioPlayer::~AudioPlayer()
{
    if (m_sink)
        m_sink->stop();
}

void AudioPlayer::write(const QByteArray &pcm)
{
    if (!m_device || pcm.isEmpty())
        return;
    const qsizetype queued = m_sink->bufferSize() - m_sink->bytesFree();
    if (queued > m_maxBuffered)
        return;
    m_device->write(pcm);
}

void AudioPlayer::setVolume(double volume)
{
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_sink)
        m_sink->setVolume(m_volume);
}

} // namespace openspeak

#pragma once
// Codificador H.264 via FFmpeg com seleção automática de hardware:
// NVENC -> AMF -> QSV -> OpenH264 (software). Um único codificador alimenta
// todos os espectadores ("single-encoding" do TS 6). Roda em thread própria
// e descarta quadros quando está atrasado (sempre codifica o mais recente).

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QVideoFrame>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace openspeak {

struct EncodedVideoFrame {
    QByteArray data; // Annex B (start codes), SPS/PPS embutidos nos keyframes
    bool keyFrame = false;
    qint64 timestampUs = 0;
    int width = 0;
    int height = 0;
};

struct VideoEncoderConfig {
    int maxWidth = 1280;  // -1 = resolução da fonte
    int maxHeight = 720;
    int fps = 30;
    int bitrate = 3'000'000;
    bool screenContent = true;
    QString preferredEncoder; // vazio = automático
};

struct VideoEncoderStats {
    QString encoder;
    int width = 0, height = 0;
    double fps = 0;
    double avgEncodeTime = 0; // s
    int currentBitrate = 0;
    QString limitReason = QStringLiteral("none"); // none | cpu | bandwidth
    quint64 framesEncoded = 0;
};

class VideoEncoder {
public:
    using Output = std::function<void(const EncodedVideoFrame &)>;

    VideoEncoder();
    ~VideoEncoder();

    /// Encoders H.264 que este FFmpeg consegue abrir, em ordem de preferência.
    static QStringList availableEncoders();

    void start(const VideoEncoderConfig &config, Output output);
    void stop();

    /// Entrega um quadro (thread-safe). Quadros acima do FPS alvo são descartados.
    void pushFrame(const QVideoFrame &frame);
    void requestKeyFrame() { m_keyFrameRequested = true; }
    void setConfig(const VideoEncoderConfig &config);
    /// Bitrate efetivo (controle de congestionamento), limitado ao configurado.
    void setTargetBitrate(int bps);
    void setPaused(bool paused) { m_paused = paused; }

    VideoEncoderStats stats() const;

private:
    void run();
    bool openCodec(const QSize &sourceSize);
    void closeCodec();
    bool encodeFrame(const QVideoFrame &frame, qint64 timestampUs);
    void drain(qint64 timestampUs);

    VideoEncoderConfig m_config;
    Output m_output;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    QVideoFrame m_pending;
    qint64 m_pendingTs = 0;
    bool m_hasPending = false;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_keyFrameRequested{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_reconfigure{false};
    std::atomic<int> m_targetBitrate{0};
    qint64 m_lastAcceptedUs = -1;

    // estado do codificador (apenas na thread do encoder)
    AVCodecContext *m_ctx = nullptr;
    AVFrame *m_frame = nullptr;
    AVPacket *m_packet = nullptr;
    SwsContext *m_sws = nullptr;
    QSize m_sourceSize;
    QString m_encoderName;
    int m_appliedBitrate = 0;
    qint64 m_lastReopenUs = 0;
    qint64 m_frameIndex = 0;

    // estatísticas
    VideoEncoderStats m_stats;
    double m_encodeTimeAcc = 0;
    int m_encodeTimeCount = 0;
    qint64 m_fpsWindowStart = 0;
    int m_fpsFrames = 0;
};

} // namespace openspeak

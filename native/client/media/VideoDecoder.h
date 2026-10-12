#pragma once
// Decodificador do lado do espectador (H.264 / AV1 via FFmpeg), em thread
// própria. Entrega QVideoFrame YUV420P para o VideoOutput da QML.

#include <QByteArray>
#include <QString>
#include <QVideoFrame>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace openspeak {

struct VideoDecoderStats {
    QString decoder;
    int width = 0, height = 0;
    double fps = 0;
    double avgDecodeTime = 0;
    quint64 framesDecoded = 0;
    quint64 decodeErrors = 0;
};

class VideoDecoder {
public:
    using Output = std::function<void(const QVideoFrame &)>;
    using KeyFrameRequest = std::function<void()>;

    VideoDecoder();
    ~VideoDecoder();

    /// `codec`: "H264" ou "AV1".
    void start(const QString &codec, Output output, KeyFrameRequest requestKeyFrame);
    void stop();
    void push(const QByteArray &accessUnit);
    VideoDecoderStats stats() const;

private:
    void run();
    bool openCodec();
    void decode(const QByteArray &au);

    QString m_codecName;
    Output m_output;
    KeyFrameRequest m_requestKeyFrame;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<QByteArray> m_queue;
    std::atomic<bool> m_running{false};

    AVCodecContext *m_ctx = nullptr;
    AVFrame *m_frame = nullptr;
    AVPacket *m_packet = nullptr;
    bool m_waitingKeyFrame = true;

    VideoDecoderStats m_stats;
    double m_timeAcc = 0;
    int m_timeCount = 0;
    qint64 m_windowStart = 0;
    int m_windowFrames = 0;
};

} // namespace openspeak

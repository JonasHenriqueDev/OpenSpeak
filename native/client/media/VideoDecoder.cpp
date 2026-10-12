#include "VideoDecoder.h"

#include <QElapsedTimer>
#include <QVideoFrameFormat>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#include <chrono>

namespace openspeak {

namespace {
constexpr size_t MaxQueue = 30;

qint64 nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

QVideoFrame toVideoFrame(const AVFrame *f)
{
    // Converte qualquer YUV 4:2:0 de 8 bits para o layout planar do Qt.
    QVideoFrameFormat format(QSize(f->width, f->height), QVideoFrameFormat::Format_YUV420P);
    format.setColorRange(f->color_range == AVCOL_RANGE_JPEG ? QVideoFrameFormat::ColorRange_Full
                                                            : QVideoFrameFormat::ColorRange_Video);
    format.setColorSpace(f->colorspace == AVCOL_SPC_BT709 ? QVideoFrameFormat::ColorSpace_BT709
                                                          : QVideoFrameFormat::ColorSpace_BT601);
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly))
        return {};
    const int heights[3] = {f->height, (f->height + 1) / 2, (f->height + 1) / 2};
    const int widths[3] = {f->width, (f->width + 1) / 2, (f->width + 1) / 2};
    for (int p = 0; p < 3; ++p) {
        uint8_t *dst = frame.bits(p);
        const int dstStride = frame.bytesPerLine(p);
        const uint8_t *src = f->data[p];
        for (int y = 0; y < heights[p]; ++y)
            memcpy(dst + y * dstStride, src + y * f->linesize[p], widths[p]);
    }
    frame.unmap();
    return frame;
}

} // namespace

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() { stop(); }

void VideoDecoder::start(const QString &codec, Output output, KeyFrameRequest requestKeyFrame)
{
    stop();
    m_codecName = codec.toUpper();
    m_output = std::move(output);
    m_requestKeyFrame = std::move(requestKeyFrame);
    m_running = true;
    m_thread = std::thread(&VideoDecoder::run, this);
}

void VideoDecoder::stop()
{
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    if (m_ctx)
        avcodec_free_context(&m_ctx);
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_packet)
        av_packet_free(&m_packet);
}

void VideoDecoder::push(const QByteArray &accessUnit)
{
    std::lock_guard lock(m_mutex);
    if (m_queue.size() >= MaxQueue) {
        // Atrasado demais: descarta e pede um keyframe para ressincronizar.
        m_queue.clear();
        m_waitingKeyFrame = true;
        if (m_requestKeyFrame)
            m_requestKeyFrame();
    }
    m_queue.push_back(accessUnit);
    m_cv.notify_one();
}

VideoDecoderStats VideoDecoder::stats() const
{
    std::lock_guard lock(m_mutex);
    return m_stats;
}

bool VideoDecoder::openCodec()
{
    const AVCodec *codec = nullptr;
    if (m_codecName == QLatin1String("AV1"))
        codec = avcodec_find_decoder_by_name("libdav1d");
    if (!codec)
        codec = avcodec_find_decoder(m_codecName == QLatin1String("AV1") ? AV_CODEC_ID_AV1 : AV_CODEC_ID_H264);
    if (!codec)
        return false;
    m_ctx = avcodec_alloc_context3(codec);
    m_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_ctx->thread_count = 2;
    m_ctx->thread_type = FF_THREAD_SLICE; // sem atraso de quadro
    if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&m_ctx);
        return false;
    }
    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();
    std::lock_guard lock(m_mutex);
    m_stats.decoder = QString::fromLatin1(codec->name);
    return true;
}

void VideoDecoder::run()
{
    if (!openCodec())
        return;
    if (m_requestKeyFrame)
        m_requestKeyFrame();
    while (m_running) {
        QByteArray au;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(200), [this] { return !m_queue.empty() || !m_running; });
            if (!m_running)
                break;
            if (m_queue.empty())
                continue;
            au = std::move(m_queue.front());
            m_queue.pop_front();
        }
        decode(au);
    }
}

void VideoDecoder::decode(const QByteArray &au)
{
    QElapsedTimer timer;
    timer.start();
    m_packet->data = reinterpret_cast<uint8_t *>(const_cast<char *>(au.constData()));
    m_packet->size = au.size();
    const int sent = avcodec_send_packet(m_ctx, m_packet);
    if (sent < 0) {
        std::lock_guard lock(m_mutex);
        m_stats.decodeErrors++;
        if (m_requestKeyFrame)
            m_requestKeyFrame();
        return;
    }
    while (avcodec_receive_frame(m_ctx, m_frame) == 0) {
        const bool usable = m_frame->format == AV_PIX_FMT_YUV420P || m_frame->format == AV_PIX_FMT_YUVJ420P;
        if (usable && m_output)
            m_output(toVideoFrame(m_frame));
        const double t = timer.nsecsElapsed() / 1e9;
        std::lock_guard lock(m_mutex);
        m_stats.width = m_frame->width;
        m_stats.height = m_frame->height;
        m_stats.framesDecoded++;
        m_timeAcc += t;
        m_timeCount++;
        m_windowFrames++;
        const qint64 now = nowUs();
        if (m_windowStart == 0)
            m_windowStart = now;
        if (now - m_windowStart >= 1'000'000) {
            m_stats.fps = m_windowFrames * 1e6 / double(now - m_windowStart);
            m_stats.avgDecodeTime = m_timeAcc / std::max(1, m_timeCount);
            m_windowStart = now;
            m_windowFrames = 0;
            m_timeAcc = 0;
            m_timeCount = 0;
        }
        av_frame_unref(m_frame);
    }
}

} // namespace openspeak

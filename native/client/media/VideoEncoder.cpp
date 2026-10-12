#include "VideoEncoder.h"

#include <QElapsedTimer>
#include <QImage>
#include <QLoggingCategory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <chrono>

Q_LOGGING_CATEGORY(lcEnc, "openspeak.encoder")

namespace openspeak {

namespace {

const char *const EncoderOrder[] = {"h264_nvenc", "h264_amf", "h264_qsv", "libopenh264"};

qint64 nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

AVPixelFormat avFormatFor(QVideoFrameFormat::PixelFormat f)
{
    switch (f) {
    case QVideoFrameFormat::Format_BGRA8888: return AV_PIX_FMT_BGRA;
    case QVideoFrameFormat::Format_BGRX8888: return AV_PIX_FMT_BGR0;
    case QVideoFrameFormat::Format_RGBA8888: return AV_PIX_FMT_RGBA;
    case QVideoFrameFormat::Format_RGBX8888: return AV_PIX_FMT_RGB0;
    case QVideoFrameFormat::Format_ARGB8888: return AV_PIX_FMT_ARGB;
    case QVideoFrameFormat::Format_XRGB8888: return AV_PIX_FMT_0RGB;
    case QVideoFrameFormat::Format_ABGR8888: return AV_PIX_FMT_ABGR;
    case QVideoFrameFormat::Format_XBGR8888: return AV_PIX_FMT_0BGR;
    case QVideoFrameFormat::Format_NV12: return AV_PIX_FMT_NV12;
    case QVideoFrameFormat::Format_NV21: return AV_PIX_FMT_NV21;
    case QVideoFrameFormat::Format_YUV420P: return AV_PIX_FMT_YUV420P;
    case QVideoFrameFormat::Format_YUV422P: return AV_PIX_FMT_YUV422P;
    case QVideoFrameFormat::Format_YUYV: return AV_PIX_FMT_YUYV422;
    case QVideoFrameFormat::Format_UYVY: return AV_PIX_FMT_UYVY422;
    default: return AV_PIX_FMT_NONE;
    }
}

bool isHardware(const QString &name) { return name != QLatin1String("libopenh264"); }

AVCodecContext *createContext(const char *name, const QSize &size, int fps, int bitrate, bool screen)
{
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (!codec)
        return nullptr;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    ctx->width = size.width();
    ctx->height = size.height();
    ctx->time_base = {1, 1'000'000};
    ctx->framerate = {fps, 1};
    ctx->pix_fmt = isHardware(QLatin1String(name)) ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    ctx->gop_size = fps * 10; // keyframes extras sob demanda (PLI / novo espectador)
    ctx->max_b_frames = 0;
    ctx->bit_rate = bitrate;
    ctx->rc_max_rate = bitrate;
    ctx->rc_buffer_size = bitrate / 2; // ~500 ms: baixa latência
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx->thread_count = isHardware(QLatin1String(name)) ? 1 : 0;

    const QByteArray n(name);
    if (n == "h264_nvenc") {
        av_opt_set(ctx->priv_data, "preset", "p3", 0);
        av_opt_set(ctx->priv_data, "tune", "ull", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        av_opt_set(ctx->priv_data, "zerolatency", "1", 0);
        av_opt_set(ctx->priv_data, "delay", "0", 0);
        av_opt_set(ctx->priv_data, "forced-idr", "1", 0);
        av_opt_set(ctx->priv_data, "profile", "baseline", 0);
    } else if (n == "h264_amf") {
        av_opt_set(ctx->priv_data, "usage", "ultralowlatency", 0);
        av_opt_set(ctx->priv_data, "quality", "speed", 0);
        av_opt_set(ctx->priv_data, "rc", "cbr", 0);
        av_opt_set(ctx->priv_data, "profile", "constrained_baseline", 0);
    } else if (n == "h264_qsv") {
        av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
        av_opt_set(ctx->priv_data, "async_depth", "1", 0);
        av_opt_set(ctx->priv_data, "low_delay_brc", "1", 0);
        av_opt_set(ctx->priv_data, "profile", "baseline", 0);
    } else if (n == "libopenh264") {
        av_opt_set(ctx->priv_data, "profile", "constrained_baseline", 0);
        av_opt_set(ctx->priv_data, "allow_skip_frames", "1", 0);
        av_opt_set(ctx->priv_data, "rc_mode", "bitrate", 0);
    }
    Q_UNUSED(screen);
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return nullptr;
    }
    return ctx;
}

} // namespace

QStringList VideoEncoder::availableEncoders()
{
    static const QStringList cached = [] {
        QStringList out;
        av_log_set_level(AV_LOG_QUIET);
        for (const char *name : EncoderOrder) {
            if (AVCodecContext *ctx = createContext(name, QSize(640, 360), 30, 1'000'000, true)) {
                out.append(QLatin1String(name));
                avcodec_free_context(&ctx);
            }
        }
        av_log_set_level(AV_LOG_WARNING);
        return out;
    }();
    return cached;
}

VideoEncoder::VideoEncoder() = default;

VideoEncoder::~VideoEncoder() { stop(); }

void VideoEncoder::start(const VideoEncoderConfig &config, Output output)
{
    stop();
    m_config = config;
    m_output = std::move(output);
    m_targetBitrate = config.bitrate;
    m_running = true;
    m_thread = std::thread(&VideoEncoder::run, this);
}

void VideoEncoder::stop()
{
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
    closeCodec();
    std::lock_guard lock(m_mutex);
    m_pending = {};
    m_hasPending = false;
}

void VideoEncoder::setConfig(const VideoEncoderConfig &config)
{
    std::lock_guard lock(m_mutex);
    m_config = config;
    m_targetBitrate = config.bitrate;
    m_reconfigure = true;
}

void VideoEncoder::setTargetBitrate(int bps)
{
    std::lock_guard lock(m_mutex);
    m_targetBitrate = std::clamp(bps, 150'000, m_config.bitrate);
}

void VideoEncoder::pushFrame(const QVideoFrame &frame)
{
    if (!m_running || m_paused)
        return;
    const qint64 ts = nowUs();
    std::lock_guard lock(m_mutex);
    const qint64 interval = 1'000'000 / std::max(1, m_config.fps);
    // Limita ao FPS alvo (com folga para jitter da captura).
    if (m_lastAcceptedUs >= 0 && ts - m_lastAcceptedUs < interval * 85 / 100)
        return;
    m_lastAcceptedUs = ts;
    m_pending = frame;
    m_pendingTs = ts;
    m_hasPending = true;
    m_cv.notify_one();
}

VideoEncoderStats VideoEncoder::stats() const
{
    std::lock_guard lock(m_mutex);
    return m_stats;
}

void VideoEncoder::run()
{
    while (m_running) {
        QVideoFrame frame;
        qint64 ts = 0;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(200), [this] { return m_hasPending || !m_running; });
            if (!m_running)
                break;
            if (!m_hasPending)
                continue;
            frame = std::move(m_pending);
            ts = m_pendingTs;
            m_pending = {};
            m_hasPending = false;
        }
        encodeFrame(frame, ts);
    }
}

bool VideoEncoder::openCodec(const QSize &sourceSize)
{
    closeCodec();
    VideoEncoderConfig cfg;
    {
        std::lock_guard lock(m_mutex);
        cfg = m_config;
    }
    // Ajusta a fonte dentro do limite mantendo a proporção; dimensões pares.
    double scale = 1.0;
    if (cfg.maxWidth > 0 && cfg.maxHeight > 0)
        scale = std::min({1.0, double(cfg.maxWidth) / sourceSize.width(), double(cfg.maxHeight) / sourceSize.height()});
    scale = std::min(scale, 4096.0 / std::max(sourceSize.width(), sourceSize.height())); // limite do NVENC/H.264
    QSize target(int(sourceSize.width() * scale) & ~1, int(sourceSize.height() * scale) & ~1);
    target = target.expandedTo(QSize(64, 64));

    const int bitrate = m_targetBitrate.load();
    QStringList order;
    if (!cfg.preferredEncoder.isEmpty())
        order.append(cfg.preferredEncoder);
    for (const QString &e : availableEncoders())
        if (!order.contains(e))
            order.append(e);
    for (const QString &name : order) {
        m_ctx = createContext(name.toLatin1().constData(), target, cfg.fps, bitrate, cfg.screenContent);
        if (m_ctx) {
            m_encoderName = name;
            break;
        }
    }
    if (!m_ctx) {
        qCWarning(lcEnc) << "nenhum codificador H.264 disponível";
        return false;
    }
    m_frame = av_frame_alloc();
    m_frame->format = m_ctx->pix_fmt;
    m_frame->width = m_ctx->width;
    m_frame->height = m_ctx->height;
    av_frame_get_buffer(m_frame, 0);
    m_packet = av_packet_alloc();
    m_sourceSize = sourceSize;
    m_appliedBitrate = bitrate;
    m_lastReopenUs = nowUs();
    m_keyFrameRequested = true;
    qCInfo(lcEnc).noquote() << "codificador" << m_encoderName << QStringLiteral("%1x%2@%3 %4 bps")
                                                                     .arg(target.width()).arg(target.height()).arg(cfg.fps).arg(bitrate);
    std::lock_guard lock(m_mutex);
    m_stats.encoder = m_encoderName;
    m_stats.width = target.width();
    m_stats.height = target.height();
    m_stats.currentBitrate = bitrate;
    return true;
}

void VideoEncoder::closeCodec()
{
    if (m_ctx)
        avcodec_free_context(&m_ctx);
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_packet)
        av_packet_free(&m_packet);
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
}

bool VideoEncoder::encodeFrame(const QVideoFrame &input, qint64 timestampUs)
{
    QVideoFrame frame = input;
    if (!frame.isValid())
        return false;
    const QSize size = frame.size();
    if (size.isEmpty())
        return false;

    const bool reconfigure = m_reconfigure.exchange(false);
    if (!m_ctx || size != m_sourceSize || reconfigure)
        if (!openCodec(size))
            return false;

    // Controle de banda: NVENC aceita troca dinâmica; os demais são reabertos.
    const int target = m_targetBitrate.load();
    if (std::abs(target - m_appliedBitrate) > m_appliedBitrate / 20) {
        if (m_encoderName == QLatin1String("h264_nvenc")) {
            m_ctx->bit_rate = target;
            m_ctx->rc_max_rate = target;
            m_ctx->rc_buffer_size = target / 2;
            m_appliedBitrate = target;
        } else if (std::abs(target - m_appliedBitrate) > m_appliedBitrate / 5 && nowUs() - m_lastReopenUs > 2'000'000) {
            if (!openCodec(size))
                return false;
        }
        std::lock_guard lock(m_mutex);
        m_stats.currentBitrate = m_appliedBitrate;
    }

    // Converte para o formato do codificador.
    AVPixelFormat srcFmt = avFormatFor(frame.pixelFormat());
    QImage fallback;
    const uint8_t *srcData[4] = {};
    int srcStride[4] = {};
    bool mapped = false;
    if (srcFmt != AV_PIX_FMT_NONE && frame.map(QVideoFrame::ReadOnly)) {
        mapped = true;
        for (int p = 0; p < frame.planeCount() && p < 4; ++p) {
            srcData[p] = frame.bits(p);
            srcStride[p] = frame.bytesPerLine(p);
        }
    } else {
        fallback = frame.toImage().convertToFormat(QImage::Format_RGBA8888);
        if (fallback.isNull())
            return false;
        srcFmt = AV_PIX_FMT_RGBA;
        srcData[0] = fallback.constBits();
        srcStride[0] = int(fallback.bytesPerLine());
    }
    m_sws = sws_getCachedContext(m_sws, size.width(), size.height(), srcFmt, m_ctx->width, m_ctx->height,
                                 m_ctx->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    av_frame_make_writable(m_frame);
    sws_scale(m_sws, srcData, srcStride, 0, size.height(), m_frame->data, m_frame->linesize);
    if (mapped)
        frame.unmap();

    m_frame->pts = timestampUs;
    if (m_keyFrameRequested.exchange(false)) {
        m_frame->pict_type = AV_PICTURE_TYPE_I;
        m_frame->flags |= AV_FRAME_FLAG_KEY;
    } else {
        m_frame->pict_type = AV_PICTURE_TYPE_NONE;
        m_frame->flags &= ~AV_FRAME_FLAG_KEY;
    }

    QElapsedTimer timer;
    timer.start();
    if (avcodec_send_frame(m_ctx, m_frame) < 0)
        return false;
    drain(timestampUs);
    const double encodeTime = timer.nsecsElapsed() / 1e9;

    // Estatísticas (janela de 1 s).
    std::lock_guard lock(m_mutex);
    m_encodeTimeAcc += encodeTime;
    m_encodeTimeCount++;
    m_fpsFrames++;
    m_stats.framesEncoded++;
    const qint64 now = nowUs();
    if (m_fpsWindowStart == 0)
        m_fpsWindowStart = now;
    if (now - m_fpsWindowStart >= 1'000'000) {
        m_stats.fps = m_fpsFrames * 1e6 / double(now - m_fpsWindowStart);
        m_stats.avgEncodeTime = m_encodeTimeAcc / std::max(1, m_encodeTimeCount);
        m_fpsWindowStart = now;
        m_fpsFrames = 0;
        m_encodeTimeAcc = 0;
        m_encodeTimeCount = 0;
        if (m_stats.avgEncodeTime > 1.0 / std::max(1, m_config.fps))
            m_stats.limitReason = QStringLiteral("cpu");
        else if (m_appliedBitrate < m_config.bitrate * 9 / 10)
            m_stats.limitReason = QStringLiteral("bandwidth");
        else
            m_stats.limitReason = QStringLiteral("none");
    }
    return true;
}

void VideoEncoder::drain(qint64 timestampUs)
{
    while (avcodec_receive_packet(m_ctx, m_packet) == 0) {
        EncodedVideoFrame out;
        out.data = QByteArray(reinterpret_cast<const char *>(m_packet->data), m_packet->size);
        out.keyFrame = m_packet->flags & AV_PKT_FLAG_KEY;
        out.timestampUs = m_packet->pts != AV_NOPTS_VALUE ? m_packet->pts : timestampUs;
        out.width = m_ctx->width;
        out.height = m_ctx->height;
        av_packet_unref(m_packet);
        if (m_output)
            m_output(out);
    }
}

} // namespace openspeak

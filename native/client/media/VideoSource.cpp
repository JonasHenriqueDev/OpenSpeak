#include "VideoSource.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QPainter>
#include <QScreen>
#include <QtMath>
#include <QtMultimedia/private/qcapturablewindow_p.h>
#include <windows.h>
#include <dwmapi.h>

namespace openspeak {

VideoSource::VideoSource(QObject *parent) : QObject(parent)
{
    m_session.setVideoSink(&m_sink);
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &VideoSource::onFrame);
    m_testTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_testTimer, &QTimer::timeout, this, &VideoSource::renderTestPattern);
}

VideoSource::~VideoSource() { close(); }

bool VideoSource::isShareableWindow(const QCapturableWindow &window)
{
    const auto *handle = QCapturableWindowPrivate::handle(window);
    const HWND hwnd = handle ? reinterpret_cast<HWND>(handle->id) : nullptr;
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd)
        return false;
    // Qt supplies "Window 0x..." for untitled shell surfaces. Use the actual
    // native title and styles rather than filtering localized application names.
    if (GetWindowTextLengthW(hwnd) <= 0) return false;
    const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return false;
    if (GetWindow(hwnd, GW_OWNER) && !(style & WS_EX_APPWINDOW)) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    wchar_t className[256]{};
    GetClassNameW(hwnd, className, 256);
    const QString cls = QString::fromWCharArray(className);
    if (cls == "Shell_TrayWnd" || cls == "Shell_SecondaryTrayWnd" || cls == "Progman" || cls == "WorkerW")
        return false;
    return true;
}

QList<SourceDescriptor> VideoSource::availableSources(StreamType type)
{
    QList<SourceDescriptor> out;
    if (type == StreamType::Screen) {
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i) {
            const QSize size = screens[i]->size() * screens[i]->devicePixelRatio();
            out.append({StreamType::Screen, QString::number(i),
                        QStringLiteral("Tela %1 (%2x%3)").arg(i + 1).arg(size.width()).arg(size.height())});
        }
    } else if (type == StreamType::Window) {
        const auto windows = QWindowCapture::capturableWindows();
        for (int i = 0; i < windows.size(); ++i) {
            const QString title = windows[i].description().trimmed();
            if (!title.isEmpty() && isShareableWindow(windows[i]))
                out.append({StreamType::Window, QString::number(i), title, false, windows[i], true});
        }
    } else if (type == StreamType::Camera) {
        for (const QCameraDevice &cam : QMediaDevices::videoInputs())
            out.append({StreamType::Camera, QString::fromUtf8(cam.id()), cam.description()});
    }
    return out;
}

bool VideoSource::open(const SourceDescriptor &desc, int targetFps)
{
    close();
    m_desc = desc;
    m_error.clear();

    if (desc.testPattern) {
        m_testStart = QDateTime::currentMSecsSinceEpoch();
        m_testFrame = 0;
        m_testTimer.start(1000 / std::max(1, targetFps));
        m_active = true;
        m_desc.label = QStringLiteral("Padrão de teste");
        return true;
    }

    if (desc.type == StreamType::Screen) {
        const auto screens = QGuiApplication::screens();
        const int idx = desc.id.toInt();
        if (idx < 0 || idx >= screens.size()) {
            m_error = QStringLiteral("no_available_source");
            return false;
        }
        m_screen = std::make_unique<QScreenCapture>();
        m_screen->setScreen(screens[idx]);
        m_session.setScreenCapture(m_screen.get());
        connect(m_screen.get(), &QScreenCapture::errorOccurred, this, [this](auto, const QString &e) { emit errorOccurred(e); });
        m_screen->start();
    } else if (desc.type == StreamType::Window) {
        const auto windows = QWindowCapture::capturableWindows();
        const int idx = desc.id.toInt();
        if ((desc.windowSpecified && !desc.window.isValid()) ||
            (!desc.windowSpecified && (idx < 0 || idx >= windows.size()))) {
            m_error = QStringLiteral("no_available_source");
            return false;
        }
        m_window = std::make_unique<QWindowCapture>();
        m_window->setWindow(desc.windowSpecified ? desc.window : windows[idx]);
        m_session.setWindowCapture(m_window.get());
        connect(m_window.get(), &QWindowCapture::errorOccurred, this, [this](auto, const QString &e) {
            emit errorOccurred(e);
            emit ended(); // janela fechada
        });
        m_window->start();
    } else if (desc.type == StreamType::Camera) {
        QCameraDevice device;
        for (const QCameraDevice &cam : QMediaDevices::videoInputs())
            if (QString::fromUtf8(cam.id()) == desc.id)
                device = cam;
        if (device.isNull() && desc.id.isEmpty())
            device = QMediaDevices::defaultVideoInput();
        if (device.isNull()) {
            m_error = QStringLiteral("no_available_source");
            return false;
        }
        m_camera = std::make_unique<QCamera>(device);
        // Escolhe o formato mais próximo de 1080p com FPS suficiente.
        QCameraFormat best;
        for (const QCameraFormat &f : device.videoFormats()) {
            if (f.maxFrameRate() + 0.5 < std::min(targetFps, 30))
                continue;
            if (best.isNull() || std::abs(f.resolution().height() - 1080) < std::abs(best.resolution().height() - 1080))
                best = f;
        }
        if (!best.isNull())
            m_camera->setCameraFormat(best);
        m_session.setCamera(m_camera.get());
        connect(m_camera.get(), &QCamera::errorOccurred, this, [this](auto, const QString &e) { emit errorOccurred(e); });
        m_camera->start();
    }
    m_active = true;
    return true;
}

void VideoSource::close()
{
    m_testTimer.stop();
    if (m_screen)
        m_screen->stop();
    if (m_window)
        m_window->stop();
    if (m_camera)
        m_camera->stop();
    m_session.setScreenCapture(nullptr);
    m_session.setWindowCapture(nullptr);
    m_session.setCamera(nullptr);
    m_screen.reset();
    m_window.reset();
    m_camera.reset();
    m_active = false;
    m_lastFrame = {};
}

void VideoSource::addPreviewSink(QVideoSink *sink)
{
    if (sink && !m_previews.contains(sink))
        m_previews.append(sink);
    if (sink && m_lastFrame.isValid())
        sink->setVideoFrame(m_lastFrame);
}

void VideoSource::removePreviewSink(QVideoSink *sink) { m_previews.removeAll(sink); }

void VideoSource::transferPreviewSinksTo(VideoSource &target)
{
    for (const auto &sink : std::as_const(m_previews))
        if (sink) target.addPreviewSink(sink);
    m_previews.clear();
}

void VideoSource::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid() || !m_active)
        return;
    m_lastFrame = frame;
    m_lastSize = frame.size();
    m_previews.removeAll(nullptr);
    for (const auto &sink : std::as_const(m_previews))
        sink->setVideoFrame(frame);
    emit frameAvailable(frame);
}

void VideoSource::renderTestPattern()
{
    const int w = 1280, h = 720;
    QImage img(w, h, QImage::Format_RGBX8888);
    const double t = (QDateTime::currentMSecsSinceEpoch() - m_testStart) / 1000.0;
    QPainter p(&img);
    p.fillRect(img.rect(), QColor::fromHsl(int(t * 40) % 360, 140, 56));
    const QColor bars[] = {Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue};
    for (int i = 0; i < 7; ++i)
        p.fillRect(QRectF(i * w / 7.0, h * 0.62, w / 7.0 + 1, h * 0.18), bars[i]);
    p.setBrush(QColor(0x16, 0xbc, 0xfb));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF((qSin(t * 1.7) * 0.4 + 0.5) * w, h * 0.35), h * 0.08, h * 0.08);
    p.setPen(Qt::white);
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(h / 12);
    p.setFont(font);
    p.drawText(QPointF(w * 0.04, h * 0.14),
               QStringLiteral("OpenSpeak %1  #%2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))).arg(m_testFrame++));
    p.end();
    onFrame(QVideoFrame(img));
}

QString VideoSource::captureThumbnail(int width) const
{
    if (!m_lastFrame.isValid())
        return {};
    QImage img = m_lastFrame.toImage();
    if (img.isNull())
        return {};
    img = img.scaledToWidth(width, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "JPEG", 65);
    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
}

} // namespace openspeak

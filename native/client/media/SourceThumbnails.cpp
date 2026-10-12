#include "SourceThumbnails.h"
#include <QBuffer>
#include <QGuiApplication>
#include <QScreen>

namespace openspeak {
SourceThumbnails::SourceThumbnails(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_next.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &SourceThumbnails::finish);
    connect(&m_next, &QTimer::timeout, this, &SourceThumbnails::next);
    connect(&m_capture, &VideoSource::frameAvailable, this, [this] {
        if (m_done || m_index < 0) return;
        m_done = true;
        const auto source = m_sources[m_index].toMap();
        emit ready(source.value("id").toString() + ":" + source.value("type").toString(),
                   m_capture.captureThumbnail());
        // Tear down the backend outside its frame callback.
        m_timeout.start(0);
    });
    connect(&m_capture, &VideoSource::errorOccurred, this, [this] { if (!m_done) m_timeout.start(0); });
}

void SourceThumbnails::start(const QVariantList &sources)
{
    stop();
    m_sources = sources;
    m_next.start(0);
}

void SourceThumbnails::stop()
{
    m_timeout.stop();
    m_next.stop();
    m_done = true;
    m_capture.close();
    m_sources.clear();
    m_index = -1;
}

void SourceThumbnails::finish()
{
    if (!m_done && m_index >= 0 && m_index < m_sources.size()) {
        const auto source = m_sources[m_index].toMap();
        emit ready(source.value("id").toString() + ":" + source.value("type").toString(), {});
    }
    m_done = true;
    m_capture.close();
    m_next.start(80);
}

void SourceThumbnails::next()
{
    if (++m_index >= m_sources.size()) return;
    const auto source = m_sources[m_index].toMap();
    SourceDescriptor d;
    d.type = StreamType(source.value("type").toInt());
    d.id = source.value("id").toString();
    d.label = source.value("label").toString();
    d.testPattern = source.value("test").toBool();
    d.window = source.value("window").value<QCapturableWindow>();
    d.windowSpecified = d.type == StreamType::Window && source.contains("window");
    m_done = false;
    if (d.type == StreamType::Screen && !d.testPattern) {
        const auto screens = QGuiApplication::screens();
        const int index = d.id.toInt();
        if (index >= 0 && index < screens.size()) {
            const auto pixmap = screens[index]->grabWindow(0);
            if (!pixmap.isNull()) {
                QByteArray jpeg;
                QBuffer buffer(&jpeg);
                buffer.open(QIODevice::WriteOnly);
                pixmap.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "JPEG", 70);
                m_done = true;
                emit ready(d.id + ":" + QString::number(int(d.type)),
                           "data:image/jpeg;base64," + QString::fromLatin1(jpeg.toBase64()));
                finish();
                return;
            }
        }
    }
    if (m_capture.open(d, 5)) m_timeout.start(1800);
    else finish();
}
}

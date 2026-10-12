#include "WindowThumbnail.h"

#include <QCapturableWindow>
#include <QQuickWindow>
#include <QtMath>
#include <QtMultimedia/private/qcapturablewindow_p.h>

namespace openspeak {
WindowThumbnail::WindowThumbnail(QQuickItem *parent) : QQuickItem(parent)
{
    m_update.setInterval(100);
    connect(&m_update, &QTimer::timeout, this, &WindowThumbnail::sync);
    connect(this, &QQuickItem::visibleChanged, this, &WindowThumbnail::sync);
    connect(this, &QQuickItem::windowChanged, this, [this] { release(); sync(); });
}

WindowThumbnail::~WindowThumbnail() { release(); }

void WindowThumbnail::release()
{
    if (!m_thumbnail) return;
    DwmUnregisterThumbnail(m_thumbnail);
    m_thumbnail = nullptr;
    emit availableChanged();
}

void WindowThumbnail::setSourceWindow(const QVariant &source)
{
    release();
    m_source = source;
    emit sourceWindowChanged();
    sync();
}

void WindowThumbnail::setPreviewEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit previewEnabledChanged();
    if (enabled) m_update.start();
    else m_update.stop();
    sync();
}

void WindowThumbnail::sync()
{
    if (!m_enabled || !isVisible() || !window() || !window()->isVisible()) {
        release();
        return;
    }
    const auto source = m_source.value<QCapturableWindow>();
    // Qt 6.11's Windows capturable-window ID is its HWND. This private access is
    // confined to the picker; revalidate it when upgrading Qt (no title matching).
    const auto *handle = QCapturableWindowPrivate::handle(source);
    const HWND sourceHwnd = handle ? reinterpret_cast<HWND>(handle->id) : nullptr;
    if (!sourceHwnd || !IsWindow(sourceHwnd)) { release(); return; }
    if (!m_thumbnail) {
        if (FAILED(DwmRegisterThumbnail(reinterpret_cast<HWND>(window()->winId()), sourceHwnd, &m_thumbnail))) return;
        emit availableChanged();
    }
    const QRectF cell = mapRectToScene(QRectF(0, 0, width(), height()));
    QRectF clip = cell.intersected(QRectF(0, 0, window()->width(), window()->height()));
    for (auto *ancestor = parentItem(); ancestor; ancestor = ancestor->parentItem())
        if (ancestor->clip()) clip = clip.intersected(ancestor->mapRectToScene(QRectF(0, 0, ancestor->width(), ancestor->height())));

    RECT client{};
    GetClientRect(sourceHwnd, &client);
    const QSizeF sourceSize(client.right - client.left, client.bottom - client.top);
    QRectF fit = cell;
    if (!sourceSize.isEmpty()) {
        const auto size = sourceSize.scaled(cell.size(), Qt::KeepAspectRatio);
        fit = QRectF(cell.center() - QPointF(size.width() / 2, size.height() / 2), size);
    }
    const QRectF destination = fit.intersected(clip);
    const qreal dpr = window()->devicePixelRatio();
    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    props.opacity = 255;
    props.fVisible = !destination.isEmpty() && !sourceSize.isEmpty();
    props.fSourceClientAreaOnly = TRUE;
    props.rcDestination = {qRound(destination.left() * dpr), qRound(destination.top() * dpr),
                           qRound(destination.right() * dpr), qRound(destination.bottom() * dpr)};
    if (props.fVisible && !fit.isEmpty()) {
        props.dwFlags |= DWM_TNP_RECTSOURCE;
        props.rcSource = {qRound((destination.left() - fit.left()) * sourceSize.width() / fit.width()),
                          qRound((destination.top() - fit.top()) * sourceSize.height() / fit.height()),
                          qRound((destination.right() - fit.left()) * sourceSize.width() / fit.width()),
                          qRound((destination.bottom() - fit.top()) * sourceSize.height() / fit.height())};
    }
    if (FAILED(DwmUpdateThumbnailProperties(m_thumbnail, &props))) release();
}
}

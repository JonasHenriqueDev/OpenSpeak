#include "NativeWindow.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QWindow>
#include <QtGui/qpa/qplatformwindow_p.h>

#include <windows.h>

NativeWindow::~NativeWindow()
{
    if (m_filterInstalled && QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

void NativeWindow::setTaskbarInFullScreen(QWindow *window, bool enabled)
{
    if (!window)
        return;
    window->create(); // a janela nativa precisa existir antes de showFullScreen()
    if (auto *native = window->nativeInterface<QNativeInterface::Private::QWindowsWindow>())
        native->setHasBorderInFullScreen(enabled);
    if (enabled && !m_borderWindows.contains(window)) {
        m_borderWindows.append(window);
        installFilter();
    }
}

void NativeWindow::enableSnapToTop(QWindow *window)
{
    if (!window || m_snapWindows.contains(window))
        return;
    m_snapWindows.append(window);
    installFilter();
}

void NativeWindow::installFilter()
{
    if (!m_filterInstalled) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
}

// Pinta de preto a área não-cliente (a borda de 1 px), sem tocar no vídeo.
static void paintBlackFrame(HWND hwnd)
{
    RECT window, client;
    if (!GetWindowRect(hwnd, &window) || !GetClientRect(hwnd, &client))
        return;
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    OffsetRect(&client, origin.x - window.left, origin.y - window.top);
    OffsetRect(&window, -window.left, -window.top);
    if (HDC dc = GetWindowDC(hwnd)) {
        ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
        FillRect(dc, &window, HBRUSH(GetStockObject(BLACK_BRUSH)));
        ReleaseDC(hwnd, dc);
    }
}

void NativeWindow::setLinkColor(const QColor &color)
{
    QPalette palette = QGuiApplication::palette();
    palette.setColor(QPalette::Link, color);
    palette.setColor(QPalette::LinkVisited, color);
    QGuiApplication::setPalette(palette);
}

bool NativeWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    switch (msg->message) {
    case WM_NCPAINT:
    case WM_NCACTIVATE:
        for (const QPointer<QWindow> &window : std::as_const(m_borderWindows)) {
            if (!window || window->visibility() != QWindow::FullScreen || HWND(window->winId()) != msg->hwnd)
                continue;
            paintBlackFrame(msg->hwnd);
            *result = msg->message == WM_NCACTIVATE ? TRUE : 0;
            return true;
        }
        break;
    case WM_ENTERSIZEMOVE:
        m_sizing = false;
        break;
    case WM_SIZING:
        m_sizing = true;
        break;
    case WM_EXITSIZEMOVE: {
        if (m_sizing)
            break;
        for (const QPointer<QWindow> &window : std::as_const(m_snapWindows)) {
            if (!window || HWND(window->winId()) != msg->hwnd)
                continue;
            POINT cursor;
            MONITORINFO info{sizeof(MONITORINFO)};
            if (GetCursorPos(&cursor) && GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info)
                && cursor.y <= info.rcMonitor.top + 1)
                QMetaObject::invokeMethod(window, &QWindow::showMaximized, Qt::QueuedConnection);
            break;
        }
        break;
    }
    }
    return false;
}

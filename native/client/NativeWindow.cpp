#include "NativeWindow.h"

#include <QCoreApplication>
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
}

void NativeWindow::enableSnapToTop(QWindow *window)
{
    if (!window || m_snapWindows.contains(window))
        return;
    m_snapWindows.append(window);
    if (!m_filterInstalled) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
}

bool NativeWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "windows_generic_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    switch (msg->message) {
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

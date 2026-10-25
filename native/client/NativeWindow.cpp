#include "NativeWindow.h"

#include <QWindow>
#include <QtGui/qpa/qplatformwindow_p.h>

void NativeWindow::setTaskbarInFullScreen(QWindow *window, bool enabled)
{
    if (!window)
        return;
    window->create(); // a janela nativa precisa existir antes de showFullScreen()
    if (auto *native = window->nativeInterface<QNativeInterface::Private::QWindowsWindow>())
        native->setHasBorderInFullScreen(enabled);
}

#include "WindowGrabber.h"

#include <QPainter>
#include <QRasterWindow>
#include <QSignalSpy>
#include <QTest>

#include <windows.h>

using namespace openspeak;

namespace {

class SolidWindow : public QRasterWindow {
public:
    explicit SolidWindow(QColor color) : m_color(color) {}

protected:
    void paintEvent(QPaintEvent *) override { QPainter(this).fillRect(QRect(QPoint(), size()), m_color); }

private:
    QColor m_color;
};

// Espera um quadro e devolve a cor do centro.
QColor centerOfNextFrame(WindowGrabber &grabber)
{
    QSignalSpy spy(&grabber, &WindowGrabber::frameReady);
    spy.clear();
    if (!spy.wait(3000) && spy.isEmpty())
        return {};
    // Descarta quadros antigos: usa o mais recente.
    QTest::qWait(300);
    const auto frame = spy.last().first().value<QVideoFrame>();
    const QImage img = frame.toImage();
    return img.isNull() ? QColor() : img.pixelColor(img.width() / 2, img.height() / 2);
}

bool similar(QColor a, QColor b) { return qAbs(a.red() - b.red()) < 16 && qAbs(a.green() - b.green()) < 16 && qAbs(a.blue() - b.blue()) < 16; }

} // namespace

class TestWindowGrabber : public QObject {
    Q_OBJECT
private slots:
    // QScreenCapture não inclui o cursor; a captura de monitor precisa desenhá-lo.
    void monitorCaptureIncludesCursor()
    {
        SolidWindow target(QColor(220, 30, 30));
        target.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        target.setGeometry(200, 200, 400, 300);
        target.show();
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        QTest::qWait(300);

        const HWND hwnd = reinterpret_cast<HWND>(target.winId());
        RECT rect{};
        GetWindowRect(hwnd, &rect);
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)};
        QVERIFY(GetMonitorInfoW(monitor, &info));
        auto grabber = WindowGrabber::forMonitor(reinterpret_cast<quintptr>(monitor), 30);
        QSignalSpy frames(grabber.get(), &WindowGrabber::frameReady);
        grabber->start();

        const QPoint spot(rect.left + 100 - info.rcMonitor.left, rect.top + 100 - info.rcMonitor.top);
        // Conta os pixels que não são da janela vermelha ao redor do ponto.
        const auto others = [&] {
            frames.clear();
            QTest::qWait(400);
            if (frames.isEmpty())
                return -1;
            const QImage img = frames.last().first().value<QVideoFrame>().toImage();
            int count = 0;
            for (int y = 0; y < 24; ++y)
                for (int x = 0; x < 16; ++x)
                    count += !similar(img.pixelColor(spot.x() + x, spot.y() + y), QColor(220, 30, 30));
            return count;
        };

        POINT saved{};
        GetCursorPos(&saved);
        SetCursorPos(0, 0);
        const int without = others();
        SetCursorPos(rect.left + 100, rect.top + 100);
        const int with = others();
        SetCursorPos(saved.x, saved.y);
        QCOMPARE(without, 0);
        QVERIFY2(with > 10, qPrintable(QString::number(with)));
    }

    void capturesVisibleAndCoveredWindowWithoutBorder()
    {
        SolidWindow target(QColor(220, 30, 30));
        target.setFlags(Qt::Window | Qt::FramelessWindowHint);
        target.setGeometry(200, 200, 400, 300);
        target.show();
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        SetForegroundWindow(reinterpret_cast<HWND>(target.winId()));
        QTest::qWait(300);

        WindowGrabber grabber(target.winId(), 30);
        grabber.start();

        // Visível e sem nada por cima: duplicação da tela recortada.
        const QColor visible = centerOfNextFrame(grabber);
        QVERIFY2(similar(visible, QColor(220, 30, 30)), qPrintable(visible.name()));

        // Coberta por outra janela: a duplicação mostraria azul; o PrintWindow mostra vermelho.
        SolidWindow cover(QColor(20, 40, 220));
        cover.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        cover.setGeometry(150, 150, 500, 400);
        cover.show();
        QVERIFY(QTest::qWaitForWindowExposed(&cover));
        QTest::qWait(300);
        const QColor covered = centerOfNextFrame(grabber);
        QVERIFY2(similar(covered, QColor(220, 30, 30)), qPrintable(covered.name()));

        grabber.stop();
    }

    void capturesAtTargetFpsNotMonitorRate()
    {
        SolidWindow target(QColor(220, 30, 30));
        target.setGeometry(200, 200, 320, 240);
        target.show();
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        WindowGrabber grabber(target.winId(), 30);
        QSignalSpy frames(&grabber, &WindowGrabber::frameReady);
        grabber.start();
        QTest::qWait(500);
        frames.clear();
        QTest::qWait(2000);
        const qsizetype n = frames.size(); // ~60 em 2 s a 30 fps
        QVERIFY2(n >= 50 && n <= 66, qPrintable(QString::number(n)));
    }

    void minimizedKeepsSendingLastFrame()
    {
        SolidWindow target(QColor(220, 30, 30));
        target.setGeometry(200, 200, 320, 240);
        target.show();
        QVERIFY(QTest::qWaitForWindowExposed(&target));
        WindowGrabber grabber(target.winId(), 30);
        QSignalSpy method(&grabber, &WindowGrabber::methodChanged);
        grabber.start();
        QVERIFY(similar(centerOfNextFrame(grabber), QColor(220, 30, 30)));
        ShowWindow(reinterpret_cast<HWND>(target.winId()), SW_MINIMIZE);
        QTRY_VERIFY_WITH_TIMEOUT(!method.isEmpty() && method.last().first().toString().contains(u"minimizada"), 2000);
        QVERIFY(similar(centerOfNextFrame(grabber), QColor(220, 30, 30)));
    }

    void reportsClosedWindow()
    {
        auto *target = new SolidWindow(Qt::green);
        target->setGeometry(200, 200, 200, 150);
        target->show();
        QVERIFY(QTest::qWaitForWindowExposed(target));
        WindowGrabber grabber(target->winId(), 30);
        QSignalSpy closed(&grabber, &WindowGrabber::windowClosed);
        grabber.start();
        QTest::qWait(200);
        delete target;
        QVERIFY(closed.wait(2000));
    }
};

QTEST_MAIN(TestWindowGrabber)
#include "tst_windowgrabber.moc"

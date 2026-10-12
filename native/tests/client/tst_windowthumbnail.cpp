#include "WindowThumbnail.h"
#include <QCapturableWindow>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QScreen>

using namespace openspeak;

class TestWindowThumbnail : public QObject {
    Q_OBJECT
private slots:
    void scrollFollowsAncestorAndClipsImmediately()
    {
        QQuickWindow source, destination;
        source.resize(320, 180);
        source.setColor(Qt::cyan);
        destination.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        destination.resize(300, 240);
        destination.setPosition(100, 100);
        destination.setColor(Qt::black);
        source.show();
        destination.show();
        ShowWindow(reinterpret_cast<HWND>(destination.winId()), SW_SHOWNORMAL);
        QQuickItem viewport(destination.contentItem());
        viewport.setPosition({30, 40});
        viewport.setSize({220, 140});
        viewport.setClip(true);
        QQuickItem content(&viewport);
        content.setSize({220, 600});
        WindowThumbnail preview(&content);
        preview.setSize({220, 124});
        preview.setSourceWindow(QVariant::fromValue(QCapturableWindow(&source)));
        preview.setPreviewEnabled(true);
        QTRY_VERIFY(preview.available());
        QTest::qWait(200);
        auto pixel = [&](int x, int y) {
            const auto point = destination.mapToGlobal(QPoint(x, y));
            return destination.screen()->grabWindow(0, point.x(), point.y(), 1, 1).toImage().pixelColor(0, 0);
        };
        QVERIFY(pixel(90, 90).green() > 200);
        content.setY(-160); // Flickable moves an ancestor, not the delegate
        DwmFlush();
        QTest::qWait(25); // under the old 100ms polling interval
        QVERIFY(pixel(90, 90).green() < 30);
        content.setY(-60);
        DwmFlush();
        QTest::qWait(25);
        QVERIFY(pixel(90, 60).green() > 200);
        QVERIFY(pixel(90, 30).green() < 30); // never outside the viewport
        QVERIFY(pixel(90, 150).green() < 30); // lower crop doesn't stretch
    }
    void compositorPreviewLifecycle()
    {
        BOOL composition = FALSE;
        QVERIFY(SUCCEEDED(DwmIsCompositionEnabled(&composition)));
        if (!composition) QSKIP("Windows desktop composition is disabled");
        QQuickWindow source, destination;
        source.resize(640, 360);
        source.setColor(Qt::cyan);
        destination.resize(400, 300);
        source.show();
        destination.show();
        QQuickItem clip(destination.contentItem());
        clip.setPosition({20, 40});
        clip.setSize({200, 140});
        clip.setClip(true);
        WindowThumbnail preview(&clip);
        preview.setSize({240, 160}); // larger than its viewport
        QSignalSpy availability(&preview, &WindowThumbnail::availableChanged);
        preview.setSourceWindow(QVariant::fromValue(QCapturableWindow(&source)));
        preview.setPreviewEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(preview.available(), 3000);
        preview.setY(-80); // scrolling a partially clipped card
        QTest::qWait(150);
        QVERIFY(preview.available());
        preview.setVisible(false);
        QVERIFY(!preview.available());
        preview.setVisible(true);
        QTRY_VERIFY(preview.available());
        preview.setPreviewEnabled(false); // dialog closed
        QVERIFY(!preview.available());
        preview.setSourceWindow({});
        preview.setPreviewEnabled(true);
        QTest::qWait(150);
        QVERIFY(!preview.available());
        QVERIFY(availability.count() >= 4);
    }
};
QTEST_MAIN(TestWindowThumbnail)
#include "tst_windowthumbnail.moc"

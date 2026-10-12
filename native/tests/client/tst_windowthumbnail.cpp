#include "WindowThumbnail.h"
#include <QCapturableWindow>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace openspeak;

class TestWindowThumbnail : public QObject {
    Q_OBJECT
private slots:
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

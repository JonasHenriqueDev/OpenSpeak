#pragma once

#include <QQuickItem>
#include <QTimer>
#include <QVariant>
#include <windows.h>
#include <dwmapi.h>

namespace openspeak {
// A compositor-only preview. It does not participate in stream capture or P2P.
class WindowThumbnail : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariant sourceWindow READ sourceWindow WRITE setSourceWindow NOTIFY sourceWindowChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled WRITE setPreviewEnabled NOTIFY previewEnabledChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
    explicit WindowThumbnail(QQuickItem *parent = nullptr);
    ~WindowThumbnail() override;
    QVariant sourceWindow() const { return m_source; }
    void setSourceWindow(const QVariant &source);
    bool previewEnabled() const { return m_enabled; }
    void setPreviewEnabled(bool enabled);
    bool available() const { return m_thumbnail != nullptr; }
signals:
    void sourceWindowChanged();
    void previewEnabledChanged();
    void availableChanged();
private:
    void watchGeometry();
    void sync();
    void release();
    QVariant m_source;
    bool m_enabled = false;
    HTHUMBNAIL m_thumbnail = nullptr;
    QTimer m_update;
    QList<QMetaObject::Connection> m_geometryConnections;
    QMetaObject::Connection m_animationConnection;
};
}

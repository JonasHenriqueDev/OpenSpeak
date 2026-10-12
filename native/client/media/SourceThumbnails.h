#pragma once

#include "VideoSource.h"
#include <QVariantList>

namespace openspeak {

// Capture one source at a time; no encoders, audio, or network publication.
class SourceThumbnails : public QObject {
    Q_OBJECT
public:
    explicit SourceThumbnails(QObject *parent = nullptr);
    Q_INVOKABLE void start(const QVariantList &sources);
    Q_INVOKABLE void stop();
signals:
    void ready(const QString &key, const QString &image);
private:
    void next();
    void finish();
    QVariantList m_sources;
    VideoSource m_capture;
    QTimer m_timeout;
    QTimer m_next;
    int m_index = -1;
    bool m_done = false;
};
}

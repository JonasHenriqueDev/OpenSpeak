#pragma once
// Fonte de captura compartilhada por todas as sessões de um stream
// (o "lado de entrada" do ScalableSharedVideoTransformer do TS).
// Telas, janelas e câmeras via Qt Multimedia; padrão de teste via QPainter.

#include "Protocol.h"

#include <QCamera>
#include <QCapturableWindow>
#include <QImage>
#include <QMediaCaptureSession>
#include <QObject>
#include <QPointer>
#include <QScreenCapture>
#include <QSet>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWindowCapture>

#include <memory>

namespace openspeak {

struct SourceDescriptor {
    StreamType type = StreamType::Screen;
    QString id;    // índice da tela, id da câmera, ou índice da janela
    QString label;
    bool testPattern = false;
};

class VideoSource : public QObject {
    Q_OBJECT
public:
    explicit VideoSource(QObject *parent = nullptr);
    ~VideoSource() override;

    /// Lista as fontes disponíveis: telas, janelas e câmeras.
    static QList<SourceDescriptor> availableSources(StreamType type);

    bool open(const SourceDescriptor &desc, int targetFps);
    void close();

    StreamType type() const { return m_desc.type; }
    QString label() const { return m_desc.label; }
    bool isActive() const { return m_active; }
    QSize lastFrameSize() const { return m_lastSize; }

    /// Encaminha cada quadro também para estes sinks (pré-visualização na QML).
    void addPreviewSink(QVideoSink *sink);
    void removePreviewSink(QVideoSink *sink);

    /// Miniatura JPEG pequena em data URL (ThumbnailCapturer do TS).
    QString captureThumbnail(int width = 320) const;

    QString errorString() const { return m_error; }

signals:
    void frameAvailable(const QVideoFrame &frame);
    void ended();
    void errorOccurred(const QString &error);

private:
    void onFrame(const QVideoFrame &frame);
    void renderTestPattern();

    SourceDescriptor m_desc;
    bool m_active = false;
    QString m_error;
    QSize m_lastSize;
    QVideoFrame m_lastFrame;
    QMediaCaptureSession m_session;
    QVideoSink m_sink;
    std::unique_ptr<QScreenCapture> m_screen;
    std::unique_ptr<QWindowCapture> m_window;
    std::unique_ptr<QCamera> m_camera;
    QTimer m_testTimer;
    qint64 m_testStart = 0;
    quint64 m_testFrame = 0;
    QList<QPointer<QVideoSink>> m_previews;
};

} // namespace openspeak

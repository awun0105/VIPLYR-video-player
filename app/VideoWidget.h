#pragma once
#include <QLabel>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPointF>
#include <atomic>
#include <mutex>

#include "viplyr/Sinks.h"

// Shows video frames with OpenGL (YUV -> RGB in a shader) and supports zoom, pan
// and rotation. Also draws the subtitle text on top of the picture.
class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions, public viplyr::IVideoSink {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    // viplyr::IVideoSink. Called from the engine's present thread.
    void present(const viplyr::VideoFrame &frame) override;

public slots:
    void setSubtitleText(const QString &text);
    void setMediaRotation(int degrees);  // rotation stored in the file
    void rotateClockwise();              // the user's extra 90 degree steps
    void resetView();                    // back to "fit the window"
    void clear();                        // show black

signals:
    void fullscreenToggleRequested();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void uploadFrame(const viplyr::VideoFrame &frame);
    void uploadPlane(int textureIndex, const uint8_t *pixels, int stride, int width, int height,
                     int bytesPerPixel);
    void setColorConversion(const viplyr::VideoFrame &frame);
    QMatrix4x4 viewTransform() const;
    QPointF toNormalized(const QPointF &widgetPosition) const;
    void layoutSubtitle();

    // Handed over from the engine thread to the GUI thread.
    std::mutex frameMutex_;
    viplyr::VideoFrame pendingFrame_;
    std::atomic<bool> updateQueued_{false};

    QOpenGLShaderProgram program_;
    GLuint textures_[3] = {0, 0, 0};
    bool hasPicture_ = false;
    bool isNv12_ = false;
    int frameWidth_ = 0;
    int frameHeight_ = 0;

    int mediaRotation_ = 0;
    int userRotation_ = 0;
    double zoom_ = 1.0;
    QPointF pan_;           // in normalized device coordinates (-1 .. 1)
    QPointF lastDragPosition_;

    QLabel *subtitleLabel_;
};

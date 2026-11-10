#pragma once
#include <QImage>
#include <QLabel>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPointF>
#include <QTimer>
#include <atomic>
#include <mutex>

#include "CudaGlInterop.h"
#include "DmaBufImporter.h"
#include "OverlayTrack.h"
#include "SubtitleRenderer.h"
#include "viplyr/MediaInfo.h"
#include "viplyr/Sinks.h"

// Picture controls applied while drawing, each from -100 to 100; 0 changes nothing.
struct PictureAdjustments {
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    int gamma = 0;
};

// Shows video frames with OpenGL and supports zoom, pan and rotation.
//
// The shader converts YUV to RGB (8-bit and 10-bit, SDR and HDR with tone
// mapping). On top of the picture the widget draws overlay shapes, subtitles,
// short messages and an optional information line.
class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions, public viplyr::IVideoSink {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    // viplyr::IVideoSink. Called from the engine's present thread.
    void present(const viplyr::VideoFrame &frame) override;

    // The picture currently shown, with rotation applied; null when there is none.
    QImage currentImage() const;
    // The GPU decoder whose frames this screen shows without copying them to the
    // CPU (Vaapi through DMA-BUF, Cuda through CUDA/OpenGL interop), or None.
    // Checks with a throwaway OpenGL context, so it works before any widget is shown.
    static viplyr::HwAccel probeGpuFramePath();
    bool isShowingGpuFrame() const { return shownOnGpu_; }  // drawn straight from the decoder
    double currentFrameTime() const { return shownFrame_.isValid() ? shownFrame_.pts() : 0.0; }

    void setPictureAdjustments(const PictureAdjustments &adjustments);
    const PictureAdjustments &pictureAdjustments() const { return adjustments_; }

    // Styled (ASS) or bitmap subtitles drawn over the picture; null removes them.
    void setPictureSubtitles(std::shared_ptr<const viplyr::SubtitleTrackData> track);

    // Shapes drawn over the video. The track must outlive the widget or be reset to null.
    void setOverlayTrack(const OverlayTrack *track);
    // A rectangle in video pixels to outline (the crop being edited); null rect hides it.
    void setCropPreview(const QRectF &rect);

public slots:
    void setSubtitleText(const QString &text);
    void setInfoText(const QString &text);    // empty hides the information line
    void showMessage(const QString &text);    // disappears by itself
    void setBottomInset(int pixels);          // keep subtitles above an overlaid control bar
    void setMediaGeometry(int rotationDegrees, double pixelAspectRatio);
    void rotateClockwise();                   // the user's extra 90 degree steps
    void resetView();                         // back to "fit the window"
    void clear();                             // show black

signals:
    void fullscreenToggleRequested();
    void frameShown(double seconds);          // a new frame is on screen
    void gpuFramePathChanged(viplyr::HwAccel decoder);  // None after a failed import

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
    bool bindHardwareFrame(const viplyr::VideoFrame &frame);
    void uploadPlane(int textureIndex, const viplyr::VideoFrame &frame, int plane, int width,
                     int height, int channels);
    void setColorUniforms(const viplyr::VideoFrame &frame, viplyr::PixelFormat storage);
    void showFrame(const viplyr::VideoFrame &frame, viplyr::PixelFormat storage, bool onGpu);
    void drawVideo(viplyr::VideoFrame &newFrame);
    void drawOverlays(QPainter &painter);

    int rotationDegrees() const { return (mediaRotation_ + userRotation_) % 360; }
    QMatrix4x4 viewTransform() const;
    QPointF videoToWidget(const QPointF &videoPixel) const;
    QPointF toNormalized(const QPointF &widgetPosition) const;
    void layoutLabels();

    // Handed over from the engine thread to the GUI thread.
    std::mutex frameMutex_;
    viplyr::VideoFrame pendingFrame_;
    std::atomic<bool> updateQueued_{false};

    QOpenGLShaderProgram program_;
    GLuint textures_[3] = {0, 0, 0};      // filled by uploads from CPU memory
    GLuint gpuTextures_[2] = {0, 0};      // views of a GPU frame (luma, chroma)
    bool shownOnGpu_ = false;
    DmaBufImporter dmaBuf_;
    CudaGlInterop cuda_;
    std::atomic<bool> acceptsGpuFrames_{false};
    viplyr::VideoFrame shownFrame_;  // kept for snapshots and overlay timing
    int frameWidth_ = 0;
    int frameHeight_ = 0;

    int mediaRotation_ = 0;
    int userRotation_ = 0;
    double pixelAspectRatio_ = 1.0;
    double zoom_ = 1.0;
    QPointF pan_;  // in normalized device coordinates (-1 .. 1)
    QPointF lastDragPosition_;

    const OverlayTrack *overlayTrack_ = nullptr;
    PictureAdjustments adjustments_;
    SubtitleRenderer pictureSubtitles_;
    QRectF cropPreview_;

    QLabel *subtitleLabel_;
    QLabel *messageLabel_;
    QLabel *infoLabel_;
    QTimer messageTimer_;
    int bottomInset_ = 0;
};

#include "VideoWidget.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kMinZoom = 1.0;
constexpr double kMaxZoom = 16.0;
constexpr double kZoomStep = 1.15;
constexpr int kSubtitleBottomMargin = 40;

const char *kVertexShader = R"(
    attribute vec2 position;
    attribute vec2 texCoord;
    uniform mat4 transform;
    varying vec2 uv;
    void main() {
        uv = texCoord;
        gl_Position = transform * vec4(position, 0.0, 1.0);
    }
)";

// Planar YUV 4:2:0 (three textures) or NV12 (luma + interleaved chroma) to RGB.
const char *kFragmentShader = R"(
    uniform sampler2D textureY;
    uniform sampler2D textureU;   // U plane, or the UV plane for NV12
    uniform sampler2D textureV;
    uniform bool isNv12;
    uniform vec3 yuvOffset;
    uniform mat3 yuvToRgb;
    varying vec2 uv;
    void main() {
        vec3 yuv;
        yuv.x = texture2D(textureY, uv).r;
        if (isNv12) {
            yuv.yz = texture2D(textureU, uv).ra;
        } else {
            yuv.y = texture2D(textureU, uv).r;
            yuv.z = texture2D(textureV, uv).r;
        }
        gl_FragColor = vec4(yuvToRgb * (yuv - yuvOffset), 1.0);
    }
)";

}  // namespace

VideoWidget::VideoWidget(QWidget *parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    subtitleLabel_ = new QLabel(this);
    subtitleLabel_->setAlignment(Qt::AlignCenter);
    subtitleLabel_->setWordWrap(true);
    subtitleLabel_->setStyleSheet("color: white; font-size: 22px; font-weight: bold; "
                                  "background-color: rgba(0, 0, 0, 150); border-radius: 5px; "
                                  "padding: 5px 15px;");
    subtitleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    subtitleLabel_->hide();
}

VideoWidget::~VideoWidget() {
    makeCurrent();
    glDeleteTextures(3, textures_);
    program_.removeAllShaders();
    doneCurrent();
}

// ------------------------------------------------------------ frame hand-over

void VideoWidget::present(const viplyr::VideoFrame &frame) {
    // The shader handles 8-bit 4:2:0 and NV12 directly; anything else is converted here,
    // on the engine thread, so the GUI thread only ever uploads.
    const viplyr::PixelFormat format = frame.format();
    const bool drawable = format == viplyr::PixelFormat::Yuv420p || format == viplyr::PixelFormat::Nv12;
    viplyr::VideoFrame ready = drawable ? frame : frame.convert(viplyr::PixelFormat::Yuv420p);
    if (!ready.isValid()) return;

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        pendingFrame_ = std::move(ready);
    }
    // One queued repaint is enough, however many frames arrive before it runs.
    if (!updateQueued_.exchange(true)) {
        QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
    }
}

void VideoWidget::clear() {
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        pendingFrame_ = viplyr::VideoFrame();
    }
    hasPicture_ = false;
    update();
}

// ------------------------------------------------------------------ rendering

void VideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.bindAttributeLocation("position", 0);
    program_.bindAttributeLocation("texCoord", 1);
    program_.link();

    glGenTextures(3, textures_);
    for (GLuint texture : textures_) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

void VideoWidget::paintGL() {
    updateQueued_ = false;
    viplyr::VideoFrame frame;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        frame = std::move(pendingFrame_);
        pendingFrame_ = viplyr::VideoFrame();
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    program_.bind();
    if (frame.isValid()) uploadFrame(frame);
    if (!hasPicture_) {
        program_.release();
        return;
    }

    // A unit quad; texture row 0 is the top of the picture.
    static const GLfloat positions[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const GLfloat texCoords[] = {0, 1, 1, 1, 0, 0, 1, 0};

    program_.setUniformValue("transform", viewTransform());
    program_.setUniformValue("textureY", 0);
    program_.setUniformValue("textureU", 1);
    program_.setUniformValue("textureV", 2);
    program_.setUniformValue("isNv12", isNv12_);
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
    }

    program_.enableAttributeArray(0);
    program_.enableAttributeArray(1);
    program_.setAttributeArray(0, GL_FLOAT, positions, 2);
    program_.setAttributeArray(1, GL_FLOAT, texCoords, 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    program_.disableAttributeArray(0);
    program_.disableAttributeArray(1);
    program_.release();
}

void VideoWidget::uploadFrame(const viplyr::VideoFrame &frame) {
    const int width = frame.width();
    const int height = frame.height();
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;

    isNv12_ = frame.format() == viplyr::PixelFormat::Nv12;
    uploadPlane(0, frame.plane(0), frame.stride(0), width, height, 1);
    if (isNv12_) {
        uploadPlane(1, frame.plane(1), frame.stride(1), chromaWidth, chromaHeight, 2);
    } else {
        uploadPlane(1, frame.plane(1), frame.stride(1), chromaWidth, chromaHeight, 1);
        uploadPlane(2, frame.plane(2), frame.stride(2), chromaWidth, chromaHeight, 1);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    setColorConversion(frame);
    frameWidth_ = width;
    frameHeight_ = height;
    hasPicture_ = true;
}

void VideoWidget::uploadPlane(int textureIndex, const uint8_t *pixels, int stride, int width,
                              int height, int bytesPerPixel) {
    const GLenum format = bytesPerPixel == 1 ? GL_LUMINANCE : GL_LUMINANCE_ALPHA;
    glActiveTexture(GL_TEXTURE0 + textureIndex);
    glBindTexture(GL_TEXTURE_2D, textures_[textureIndex]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Rows are `stride` bytes apart, which may be more than the visible width.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / bytesPerPixel);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
}

void VideoWidget::setColorConversion(const viplyr::VideoFrame &frame) {
    // Luma weights of the two common standards.
    const float kr = frame.isBt709() ? 0.2126f : 0.299f;
    const float kb = frame.isBt709() ? 0.0722f : 0.114f;
    const float kg = 1.0f - kr - kb;

    // Limited range stores luma in 16..235 and chroma in 16..240.
    const bool full = frame.isFullRange();
    const float lumaScale = full ? 1.0f : 255.0f / 219.0f;
    const float chromaScale = full ? 1.0f : 255.0f / 224.0f;
    const float lumaOffset = full ? 0.0f : 16.0f / 255.0f;

    const float redFromV = 2.0f * (1.0f - kr) * chromaScale;
    const float blueFromU = 2.0f * (1.0f - kb) * chromaScale;
    const float greenFromU = -2.0f * kb * (1.0f - kb) / kg * chromaScale;
    const float greenFromV = -2.0f * kr * (1.0f - kr) / kg * chromaScale;

    // Row-major: each row produces one of R, G, B from (Y, U, V).
    const float matrix[9] = {
        lumaScale, 0.0f,       redFromV,
        lumaScale, greenFromU, greenFromV,
        lumaScale, blueFromU,  0.0f,
    };
    program_.setUniformValue("yuvToRgb", QMatrix3x3(matrix));
    program_.setUniformValue("yuvOffset", QVector3D(lumaOffset, 0.5f, 0.5f));
}

QMatrix4x4 VideoWidget::viewTransform() const {
    const int rotation = (mediaRotation_ + userRotation_) % 360;
    const bool sideways = rotation % 180 != 0;
    const double shownWidth = sideways ? frameHeight_ : frameWidth_;
    const double shownHeight = sideways ? frameWidth_ : frameHeight_;
    const double fit = std::min(width() / shownWidth, height() / shownHeight);

    // Read bottom-up: size the unit quad to the picture, rotate it, scale it to
    // fit the window, then apply the user's zoom and pan.
    QMatrix4x4 transform;
    transform.translate(pan_.x(), pan_.y());
    transform.scale(zoom_, zoom_);
    transform.scale(2.0 * fit / width(), 2.0 * fit / height());
    transform.rotate(-rotation, 0.0f, 0.0f, 1.0f);  // negative = clockwise on screen
    transform.scale(frameWidth_ / 2.0, frameHeight_ / 2.0);
    return transform;
}

// ------------------------------------------------------------- view controls

void VideoWidget::setMediaRotation(int degrees) {
    mediaRotation_ = degrees;
    userRotation_ = 0;
    resetView();
}

void VideoWidget::rotateClockwise() {
    userRotation_ = (userRotation_ + 90) % 360;
    resetView();
}

void VideoWidget::resetView() {
    zoom_ = 1.0;
    pan_ = QPointF();
    update();
}

QPointF VideoWidget::toNormalized(const QPointF &widgetPosition) const {
    return QPointF(2.0 * widgetPosition.x() / width() - 1.0,
                   1.0 - 2.0 * widgetPosition.y() / height());
}

void VideoWidget::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) return;  // horizontal scrolling does not zoom

    const double newZoom = std::clamp(zoom_ * (delta > 0 ? kZoomStep : 1.0 / kZoomStep),
                                      kMinZoom, kMaxZoom);
    // Keep the point under the cursor where it is.
    const QPointF cursor = toNormalized(event->position());
    pan_ = cursor - (cursor - pan_) * (newZoom / zoom_);
    zoom_ = newZoom;
    if (zoom_ == kMinZoom) pan_ = QPointF();
    update();
}

void VideoWidget::mousePressEvent(QMouseEvent *event) {
    setFocus();
    lastDragPosition_ = event->position();
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton) || zoom_ <= kMinZoom) return;
    const QPointF delta = event->position() - lastDragPosition_;
    lastDragPosition_ = event->position();
    pan_ += QPointF(2.0 * delta.x() / width(), -2.0 * delta.y() / height());
    update();
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) emit fullscreenToggleRequested();
}

// ------------------------------------------------------------------ subtitles

void VideoWidget::setSubtitleText(const QString &text) {
    subtitleLabel_->setText(text);
    subtitleLabel_->setVisible(!text.isEmpty());
    layoutSubtitle();
}

void VideoWidget::layoutSubtitle() {
    if (subtitleLabel_->isHidden()) return;
    // One line when it fits; otherwise wrap inside 90% of the widget's width.
    const int maximumWidth = width() * 9 / 10;
    subtitleLabel_->setWordWrap(false);
    subtitleLabel_->setMinimumWidth(0);
    subtitleLabel_->setMaximumWidth(QWIDGETSIZE_MAX);
    subtitleLabel_->adjustSize();
    if (subtitleLabel_->width() > maximumWidth) {
        subtitleLabel_->setWordWrap(true);
        subtitleLabel_->setFixedWidth(maximumWidth);
        subtitleLabel_->adjustSize();
    }
    subtitleLabel_->move((width() - subtitleLabel_->width()) / 2,
                         height() - subtitleLabel_->height() - kSubtitleBottomMargin);
}

void VideoWidget::resizeEvent(QResizeEvent *event) {
    QOpenGLWidget::resizeEvent(event);
    layoutSubtitle();
}

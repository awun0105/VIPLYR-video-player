#include "VideoWidget.h"

#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QVector4D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kMinZoom = 1.0;
constexpr double kMaxZoom = 16.0;
constexpr double kZoomStep = 1.15;
constexpr int kSubtitleBottomMargin = 24;
constexpr int kLabelMargin = 14;
constexpr int kMessageDurationMs = 1400;

const char *kOverlayLabelStyle =
    "color: white; font-size: 14px; font-weight: bold; background-color: rgba(0, 0, 0, 140); "
    "border-radius: 6px; padding: 5px 10px;";

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

// Planar YUV 4:2:0 (three textures) or semi-planar (luma + interleaved chroma) to RGB,
// with tone mapping to the screen's range for HDR video.
const char *kFragmentShader = R"(
    uniform sampler2D textureY;
    uniform sampler2D textureU;   // U plane, or the interleaved UV plane
    uniform sampler2D textureV;
    uniform bool semiPlanar;
    uniform float sampleScale;    // stretches 10-bit samples stored in 16 bits to 0..1
    uniform vec3 yuvOffset;
    uniform mat3 yuvToRgb;
    uniform int transfer;         // 0 = SDR, 1 = PQ (HDR10), 2 = HLG
    uniform vec4 adjustments;     // brightness offset, contrast, saturation, gamma exponent
    varying vec2 uv;

    // BT.2020 primaries to the screen's BT.709 primaries.
    const mat3 bt2020ToBt709 = mat3( 1.6605, -0.1246, -0.0182,
                                    -0.5876,  1.1329, -0.1006,
                                    -0.0728, -0.0083,  1.1187);

    // PQ signal -> light, where 1.0 is SDR white (203 nits).
    vec3 pqToLinear(vec3 signal) {
        const float m1 = 0.1593017578125, m2 = 78.84375;
        const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        vec3 p = pow(clamp(signal, 0.0, 1.0), vec3(1.0 / m2));
        vec3 nits = 10000.0 * pow(max(p - c1, 0.0) / (c2 - c3 * p), vec3(1.0 / m1));
        return nits / 203.0;
    }

    // HLG signal -> light for a 1000-nit display, where 1.0 is SDR white.
    vec3 hlgToLinear(vec3 signal) {
        const float a = 0.17883277, b = 0.28466892, c = 0.55991073;
        vec3 low = signal * signal / 3.0;
        vec3 high = (exp((signal - c) / a) + b) / 12.0;
        vec3 scene = mix(low, high, step(0.5, signal));
        float luminance = dot(scene, vec3(0.2627, 0.6780, 0.0593));
        return scene * pow(max(luminance, 1e-6), 0.2) * (1000.0 / 203.0);
    }

    // Filmic curve (Hable): compresses bright highlights instead of clipping them.
    vec3 hable(vec3 x) {
        const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
        return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    }

    void main() {
        vec3 yuv;
        yuv.x = texture2D(textureY, uv).r;
        if (semiPlanar) {
            yuv.yz = texture2D(textureU, uv).rg;
        } else {
            yuv.y = texture2D(textureU, uv).r;
            yuv.z = texture2D(textureV, uv).r;
        }
        vec3 rgb = yuvToRgb * (yuv * sampleScale - yuvOffset);

        if (transfer != 0) {
            vec3 light = transfer == 1 ? pqToLinear(rgb) : hlgToLinear(rgb);
            light = max(bt2020ToBt709 * light, 0.0);
            const float whitePoint = 11.2;
            light = hable(light * 2.0) / hable(vec3(whitePoint));
            rgb = pow(clamp(light, 0.0, 1.0), vec3(1.0 / 2.2));
        }

        // The viewer's picture adjustments (all neutral by default).
        rgb = pow(clamp(rgb, 0.0, 1.0), vec3(adjustments.w));
        rgb = (rgb - 0.5) * adjustments.y + 0.5 + adjustments.x;
        rgb = mix(vec3(dot(rgb, vec3(0.2126, 0.7152, 0.0722))), rgb, adjustments.z);
        gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
    }
)";

bool isDrawable(viplyr::PixelFormat format) {
    return format == viplyr::PixelFormat::Yuv420p || format == viplyr::PixelFormat::Nv12 ||
           format == viplyr::PixelFormat::Yuv420p10 || format == viplyr::PixelFormat::P010;
}

QLabel *createOverlayLabel(QWidget *parent) {
    QLabel *label = new QLabel(parent);
    label->setStyleSheet(kOverlayLabelStyle);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->hide();
    return label;
}

}  // namespace

VideoWidget::VideoWidget(QWidget *parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    subtitleLabel_ = createOverlayLabel(this);
    subtitleLabel_->setAlignment(Qt::AlignCenter);
    subtitleLabel_->setStyleSheet("color: white; font-size: 22px; font-weight: bold; "
                                  "background-color: rgba(0, 0, 0, 150); border-radius: 5px; "
                                  "padding: 5px 15px;");
    messageLabel_ = createOverlayLabel(this);
    infoLabel_ = createOverlayLabel(this);
    infoLabel_->setStyleSheet(QString(kOverlayLabelStyle) + "font-family: monospace; font-size: 12px;");

    messageTimer_.setSingleShot(true);
    messageTimer_.setInterval(kMessageDurationMs);
    connect(&messageTimer_, &QTimer::timeout, messageLabel_, &QLabel::hide);
}

VideoWidget::~VideoWidget() {
    makeCurrent();
    dmaBuf_.release();
    cuda_.release();
    glDeleteTextures(3, textures_);
    glDeleteTextures(2, gpuTextures_);
    program_.removeAllShaders();
    doneCurrent();
}

// ------------------------------------------------------------ frame hand-over

void VideoWidget::present(const viplyr::VideoFrame &frame) {
    // GPU frames go straight to the screen when the context can import them.
    // Anything else the shader cannot read is converted here, on the engine
    // thread, so the GUI thread only ever uploads.
    const bool passThrough = isDrawable(frame.format()) || (frame.isHardware() && acceptsGpuFrames_);
    viplyr::VideoFrame ready = passThrough ? frame : frame.convert(viplyr::PixelFormat::Yuv420p);
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
    shownFrame_ = viplyr::VideoFrame();
    update();
}

QImage VideoWidget::currentImage() const {
    const viplyr::VideoFrame rgba = shownFrame_.convert(viplyr::PixelFormat::Rgba);
    if (!rgba.isValid()) return QImage();
    // copy(): the QImage must own its pixels once the converted frame is released.
    const QImage image = QImage(rgba.plane(0), rgba.width(), rgba.height(), rgba.stride(0),
                                QImage::Format_RGBA8888).copy();
    const int rotation = rotationDegrees();
    return rotation == 0 ? image : image.transformed(QTransform().rotate(rotation));
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
    glGenTextures(2, gpuTextures_);
    for (GLuint texture : {textures_[0], textures_[1], textures_[2], gpuTextures_[0], gpuTextures_[1]}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    viplyr::HwAccel path = viplyr::HwAccel::None;
    // CUDA first: it only accepts an OpenGL context on the NVIDIA GPU, where NVDEC
    // decodes; any other GPU takes VAAPI frames.
    if (cuda_.initialize()) path = viplyr::HwAccel::Cuda;
    else if (dmaBuf_.initialize()) path = viplyr::HwAccel::Vaapi;
    acceptsGpuFrames_ = path != viplyr::HwAccel::None;
    emit gpuFramePathChanged(path);
}

viplyr::HwAccel VideoWidget::probeGpuFramePath() {
    QOpenGLContext context;
    QOffscreenSurface surface;
    surface.create();
    if (!context.create() || !context.makeCurrent(&surface)) return viplyr::HwAccel::None;
    viplyr::HwAccel path = viplyr::HwAccel::None;
    DmaBufImporter dmaBuf;
    CudaGlInterop cuda;
    if (cuda.initialize()) path = viplyr::HwAccel::Cuda;
    else if (dmaBuf.initialize()) path = viplyr::HwAccel::Vaapi;
    context.doneCurrent();
    return path;
}

void VideoWidget::paintGL() {
    updateQueued_ = false;
    viplyr::VideoFrame newFrame;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        newFrame = std::move(pendingFrame_);
        pendingFrame_ = viplyr::VideoFrame();
    }

    // The video is drawn with OpenGL directly, the overlays with QPainter. Wrapping our
    // calls in native painting makes QPainter resync the state it caches (bound textures,
    // program), which our calls change behind its back.
    QPainter painter(this);
    painter.beginNativePainting();
    drawVideo(newFrame);
    painter.endNativePainting();

    drawOverlays(painter);
    if (newFrame.isValid()) emit frameShown(shownFrame_.pts());
}

void VideoWidget::drawVideo(viplyr::VideoFrame &newFrame) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    program_.bind();
    if (newFrame.isHardware() && !bindHardwareFrame(newFrame)) {
        // The import failed (another GPU, an unusual surface): copy this frame and
        // ask for CPU frames from now on.
        acceptsGpuFrames_ = false;
        emit gpuFramePathChanged(viplyr::HwAccel::None);
        newFrame = newFrame.download();
    }
    if (newFrame.isValid() && !newFrame.isHardware()) uploadFrame(newFrame);
    if (!shownFrame_.isValid()) {
        program_.release();
        return;
    }

    // A unit quad; texture row 0 is the top of the picture.
    static const GLfloat positions[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const GLfloat texCoords[] = {0, 1, 1, 1, 0, 0, 1, 0};

    program_.setUniformValue("transform", viewTransform());
    // -100..100 each: brightness shifts, contrast and saturation scale (0 to 2x), gamma
    // bends the mid-tones (an exponent from 1/4 to 4).
    program_.setUniformValue("adjustments",
                             QVector4D(adjustments_.brightness / 200.0f, 1.0f + adjustments_.contrast / 100.0f,
                                       1.0f + adjustments_.saturation / 100.0f,
                                       std::pow(4.0f, -adjustments_.gamma / 100.0f)));
    program_.setUniformValue("textureY", 0);
    program_.setUniformValue("textureU", 1);
    program_.setUniformValue("textureV", 2);
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, shownOnGpu_ && i < 2 ? gpuTextures_[i] : textures_[i]);
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
    const viplyr::PixelFormat format = frame.format();
    const bool semiPlanar = format == viplyr::PixelFormat::Nv12 || format == viplyr::PixelFormat::P010;

    uploadPlane(0, frame, 0, width, height, 1);
    if (semiPlanar) {
        uploadPlane(1, frame, 1, chromaWidth, chromaHeight, 2);
    } else {
        uploadPlane(1, frame, 1, chromaWidth, chromaHeight, 1);
        uploadPlane(2, frame, 2, chromaWidth, chromaHeight, 1);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    showFrame(frame, format, false);
}

bool VideoWidget::bindHardwareFrame(const viplyr::VideoFrame &frame) {
    if (cuda_.isAvailable()) {
        const viplyr::CudaImage image = frame.cudaPlanes();
        if (!image.isValid() || !cuda_.copy(image, gpuTextures_)) return false;
        showFrame(frame, image.format, true);
        return true;
    }
    if (dmaBuf_.isAvailable()) {
        const viplyr::DmaBufImage image = frame.exportDmaBuf();
        if (!image.isValid() || !dmaBuf_.bind(image, gpuTextures_)) return false;
        showFrame(frame, image.format, true);
        return true;
    }
    return false;
}

void VideoWidget::showFrame(const viplyr::VideoFrame &frame, viplyr::PixelFormat storage, bool onGpu) {
    const bool semiPlanar = storage == viplyr::PixelFormat::Nv12 || storage == viplyr::PixelFormat::P010;
    program_.setUniformValue("semiPlanar", semiPlanar);
    setColorUniforms(frame, storage);
    if (!onGpu) dmaBuf_.release();  // the GPU frame is no longer drawn
    shownOnGpu_ = onGpu;
    frameWidth_ = frame.width();
    frameHeight_ = frame.height();
    shownFrame_ = frame;
}

void VideoWidget::uploadPlane(int textureIndex, const viplyr::VideoFrame &frame, int plane, int width,
                              int height, int channels) {
    const viplyr::PixelFormat pixelFormat = frame.format();
    const bool sixteenBit = pixelFormat == viplyr::PixelFormat::Yuv420p10 ||
                            pixelFormat == viplyr::PixelFormat::P010;
    const int bytesPerSample = sixteenBit ? 2 : 1;

    const GLenum format = channels == 1 ? GL_RED : GL_RG;
    GLint internalFormat;
    if (sixteenBit) internalFormat = channels == 1 ? GL_R16 : GL_RG16;
    else internalFormat = channels == 1 ? GL_R8 : GL_RG8;

    glActiveTexture(GL_TEXTURE0 + textureIndex);
    glBindTexture(GL_TEXTURE_2D, textures_[textureIndex]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Rows are `stride` bytes apart, which may be more than the visible width.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.stride(plane) / (bytesPerSample * channels));
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format,
                 sixteenBit ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, frame.plane(plane));
}

void VideoWidget::setColorUniforms(const viplyr::VideoFrame &frame, viplyr::PixelFormat storage) {
    // Luma weights of the colour standards.
    float kr = 0.299f, kb = 0.114f;                                      // BT.601
    if (frame.isBt2020()) { kr = 0.2627f; kb = 0.0593f; }
    else if (frame.isBt709()) { kr = 0.2126f; kb = 0.0722f; }
    const float kg = 1.0f - kr - kb;

    // Limited range stores luma in 16..235 and chroma in 16..240 (of 255).
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

    // 10-bit samples sit in the low bits of a 16-bit texel (planar) or the high bits (P010).
    const bool lowBits = storage == viplyr::PixelFormat::Yuv420p10;
    program_.setUniformValue("sampleScale", lowBits ? 65535.0f / 1023.0f : 1.0f);

    int transfer = 0;
    if (frame.transfer() == viplyr::TransferFunction::Pq) transfer = 1;
    else if (frame.transfer() == viplyr::TransferFunction::Hlg) transfer = 2;
    program_.setUniformValue("transfer", transfer);
}

QMatrix4x4 VideoWidget::viewTransform() const {
    const int rotation = rotationDegrees();
    const bool sideways = rotation % 180 != 0;
    // Non-square pixels make the picture wider or narrower than its pixel count.
    const double pictureWidth = frameWidth_ * pixelAspectRatio_;
    const double pictureHeight = frameHeight_;
    const double shownWidth = sideways ? pictureHeight : pictureWidth;
    const double shownHeight = sideways ? pictureWidth : pictureHeight;
    const double fit = std::min(width() / shownWidth, height() / shownHeight);

    // Read bottom-up: size the unit quad to the picture, rotate it, scale it to
    // fit the window, then apply the user's zoom and pan.
    QMatrix4x4 transform;
    transform.translate(pan_.x(), pan_.y());
    transform.scale(zoom_, zoom_);
    transform.scale(2.0 * fit / width(), 2.0 * fit / height());
    transform.rotate(-rotation, 0.0f, 0.0f, 1.0f);  // negative = clockwise on screen
    transform.scale(pictureWidth / 2.0, pictureHeight / 2.0);
    return transform;
}

// ------------------------------------------------------------------- overlays

QPointF VideoWidget::videoToWidget(const QPointF &videoPixel) const {
    const QPointF quad(2.0 * videoPixel.x() / frameWidth_ - 1.0, 1.0 - 2.0 * videoPixel.y() / frameHeight_);
    const QPointF normalized = viewTransform().map(quad);
    return QPointF((normalized.x() + 1.0) * width() / 2.0, (1.0 - normalized.y()) * height() / 2.0);
}

void VideoWidget::drawOverlays(QPainter &painter) {
    const QVector<OverlayShape> shapes =
        overlayTrack_ ? overlayTrack_->shapesAt(shownFrame_.pts()) : QVector<OverlayShape>();
    if (!shownFrame_.isValid() || (shapes.isEmpty() && cropPreview_.isNull() && pictureSubtitles_.isEmpty())) return;
    painter.setRenderHint(QPainter::Antialiasing);

    if (!pictureSubtitles_.isEmpty()) {
        // Map video pixels onto the picture as shown (fit, zoom, pan, rotation, pixel shape).
        const QPolygonF video({QPointF(0, 0), QPointF(frameWidth_, 0), QPointF(frameWidth_, frameHeight_),
                               QPointF(0, frameHeight_)});
        const QPolygonF screen({videoToWidget(video[0]), videoToWidget(video[1]), videoToWidget(video[2]),
                                videoToWidget(video[3])});
        QTransform toScreen;
        if (QTransform::quadToQuad(video, screen, toScreen)) {
            const QSize screenSize(qRound(QLineF(screen[0], screen[1]).length()),
                                   qRound(QLineF(screen[1], screen[2]).length()));
            painter.save();
            painter.setTransform(toScreen);
            pictureSubtitles_.draw(painter, shownFrame_.pts(), QSize(frameWidth_, frameHeight_), screenSize);
            painter.restore();
        }
    }

    // A rectangle in the video becomes a (possibly rotated) polygon on screen.
    const auto toPolygon = [this](const QRectF &rect) {
        return QPolygonF({videoToWidget(rect.topLeft()), videoToWidget(rect.topRight()),
                          videoToWidget(rect.bottomRight()), videoToWidget(rect.bottomLeft())});
    };
    const auto drawLabel = [&painter](const QPointF &anchor, const OverlayShape &shape) {
        if (shape.label.isEmpty()) return;
        const QRectF textBox = painter.fontMetrics().boundingRect(shape.label).adjusted(-4, -2, 4, 2);
        const QRectF placed = textBox.translated(anchor - textBox.bottomLeft() - QPointF(0, 3));
        painter.fillRect(placed, shape.color);
        painter.setPen(Qt::black);
        painter.drawText(placed, Qt::AlignCenter, shape.label);
    };

    for (const OverlayShape &shape : shapes) {
        painter.setPen(QPen(shape.color, 2));
        painter.setBrush(Qt::NoBrush);
        if (shape.type == OverlayShape::Type::Box) {
            const QPolygonF polygon = toPolygon(shape.box);
            painter.drawPolygon(polygon);
            drawLabel(polygon.boundingRect().topLeft(), shape);
        } else {
            const QPointF centre = videoToWidget(shape.point);
            painter.setBrush(shape.color);
            painter.drawEllipse(centre, 4, 4);
            drawLabel(centre + QPointF(6, -4), shape);
        }
    }

    if (!cropPreview_.isNull()) {
        painter.setPen(QPen(QColor(255, 214, 0), 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(toPolygon(cropPreview_));
    }
}

void VideoWidget::setPictureAdjustments(const PictureAdjustments &adjustments) {
    adjustments_ = adjustments;
    update();
}

void VideoWidget::setPictureSubtitles(std::shared_ptr<const viplyr::SubtitleTrackData> track) {
    pictureSubtitles_.setTrack(std::move(track));
    update();
}

void VideoWidget::setOverlayTrack(const OverlayTrack *track) {
    overlayTrack_ = track;
    update();
}

void VideoWidget::setCropPreview(const QRectF &rect) {
    cropPreview_ = rect;
    update();
}

// ------------------------------------------------------------- view controls

void VideoWidget::setMediaGeometry(int rotationDegrees, double pixelAspectRatio) {
    mediaRotation_ = rotationDegrees;
    pixelAspectRatio_ = pixelAspectRatio > 0.0 ? pixelAspectRatio : 1.0;
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

// --------------------------------------------------------------------- labels

void VideoWidget::setSubtitleText(const QString &text) {
    subtitleLabel_->setText(text);
    subtitleLabel_->setVisible(!text.isEmpty());
    layoutLabels();
}

void VideoWidget::setInfoText(const QString &text) {
    infoLabel_->setText(text);
    infoLabel_->setVisible(!text.isEmpty());
    layoutLabels();
}

void VideoWidget::showMessage(const QString &text) {
    messageLabel_->setText(text);
    messageLabel_->adjustSize();
    messageLabel_->move(kLabelMargin, kLabelMargin);
    messageLabel_->show();
    messageLabel_->raise();
    messageTimer_.start();
}

void VideoWidget::setBottomInset(int pixels) {
    bottomInset_ = pixels;
    layoutLabels();
}

void VideoWidget::layoutLabels() {
    if (!infoLabel_->isHidden()) {
        infoLabel_->adjustSize();
        infoLabel_->move(width() - infoLabel_->width() - kLabelMargin, kLabelMargin);
    }
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
                         height() - subtitleLabel_->height() - kSubtitleBottomMargin - bottomInset_);
}

void VideoWidget::resizeEvent(QResizeEvent *event) {
    QOpenGLWidget::resizeEvent(event);
    layoutLabels();
}

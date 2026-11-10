#pragma once
#include <QOpenGLFunctions>

#include "viplyr/Frame.h"

// Turns the DMA-BUF planes of a GPU-decoded frame into OpenGL textures through
// EGL, so the picture goes from the decoder to the screen without a copy.
//
// Works only when the widget's OpenGL context is an EGL context with DMA-BUF
// import; initialize() tells. All calls need that context to be current.
class DmaBufImporter {
public:
    DmaBufImporter() = default;
    ~DmaBufImporter();
    DmaBufImporter(const DmaBufImporter &) = delete;
    DmaBufImporter &operator=(const DmaBufImporter &) = delete;

    bool initialize();
    bool isAvailable() const { return available_; }

    // Points textures[0] (luma) and textures[1] (chroma) at the image. The
    // previous image is released only now, after it is no longer drawn.
    bool bind(const viplyr::DmaBufImage &image, const GLuint textures[2]);
    void release();

private:
    using ImageTargetTexture = void (*)(GLenum target, void *image);
    using CreateImage = void *(*)(void *display, void *context, unsigned target, void *buffer,
                                  const int *attributes);
    using DestroyImage = unsigned (*)(void *display, void *image);

    void destroyImages(void *const images[2]);

    bool available_ = false;
    void *display_ = nullptr;
    ImageTargetTexture imageTargetTexture_ = nullptr;
    CreateImage createImage_ = nullptr;
    DestroyImage destroyImage_ = nullptr;

    void *images_[2] = {nullptr, nullptr};
    viplyr::DmaBufImage boundImage_;
};

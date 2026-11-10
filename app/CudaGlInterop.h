#pragma once
#include <QOpenGLFunctions>

#include "viplyr/Frame.h"

// Copies the planes of a CUDA (NVDEC) frame into OpenGL textures on the GPU,
// GPU memory to GPU memory, so the picture never passes through the CPU.
//
// Needs the NVIDIA driver (libcuda is loaded at run time, so the program still
// starts without it) and an OpenGL context on the NVIDIA GPU; initialize() tells.
// All calls need that context to be current.
class CudaGlInterop {
public:
    CudaGlInterop() = default;
    ~CudaGlInterop();
    CudaGlInterop(const CudaGlInterop &) = delete;
    CudaGlInterop &operator=(const CudaGlInterop &) = delete;

    bool initialize();
    bool isAvailable() const { return context_ != nullptr; }

    // Fills textures[0] (luma) and textures[1] (chroma), resizing them when needed.
    bool copy(const viplyr::CudaImage &image, const GLuint textures[2]);
    void release();  // unregisters the textures; call before deleting them

private:
    bool registerTextures(const viplyr::CudaImage &image, const GLuint textures[2]);

    void *context_ = nullptr;          // CUcontext, the device's primary context
    void *resources_[2] = {nullptr, nullptr};  // CUgraphicsResource per texture
    GLuint registeredTextures_[2] = {0, 0};
    viplyr::PixelFormat registeredFormat_ = viplyr::PixelFormat::Unknown;
    int registeredWidth_ = 0;
    int registeredHeight_ = 0;
};

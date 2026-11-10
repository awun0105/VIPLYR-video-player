#include "CudaGlInterop.h"

#include <QOpenGLContext>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>

namespace {

// The few CUDA driver API declarations used here, so building needs no CUDA SDK.
using CUresult = int;
using CUdevice = int;
using CUcontext = void *;
using CUgraphicsResource = void *;
using CUarray = void *;
using CUstream = void *;
using CUdeviceptr = unsigned long long;

constexpr CUresult kSuccess = 0;
constexpr unsigned kRegisterWriteDiscard = 2;  // CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD
constexpr int kMemoryDevice = 2;               // CU_MEMORYTYPE_DEVICE
constexpr int kMemoryArray = 3;                // CU_MEMORYTYPE_ARRAY
// FFmpeg's decoder shares the primary context only with these flags set.
constexpr unsigned kContextBlockingSync = 4;   // CU_CTX_SCHED_BLOCKING_SYNC

struct Memcpy2D {  // CUDA_MEMCPY2D
    size_t srcXInBytes, srcY;
    int srcMemoryType;
    const void *srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    size_t srcPitch;
    size_t dstXInBytes, dstY;
    int dstMemoryType;
    void *dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    size_t dstPitch;
    size_t WidthInBytes, Height;
};

struct CudaDriver {
    CUresult (*init)(unsigned);
    CUresult (*deviceGet)(CUdevice *, int);
    CUresult (*primaryCtxRetain)(CUcontext *, CUdevice);
    CUresult (*primaryCtxRelease)(CUdevice);
    CUresult (*primaryCtxSetFlags)(CUdevice, unsigned);
    CUresult (*ctxPush)(CUcontext);
    CUresult (*ctxPop)(CUcontext *);
    CUresult (*registerImage)(CUgraphicsResource *, unsigned texture, unsigned target, unsigned flags);
    CUresult (*unregisterResource)(CUgraphicsResource);
    CUresult (*mapResources)(unsigned count, CUgraphicsResource *, CUstream);
    CUresult (*unmapResources)(unsigned count, CUgraphicsResource *, CUstream);
    CUresult (*mappedArray)(CUarray *, CUgraphicsResource, unsigned index, unsigned level);
    CUresult (*memcpy2DAsync)(const Memcpy2D *, CUstream);
    bool loaded = false;
};

// Loads libcuda once; null when the NVIDIA driver is missing.
const CudaDriver *cudaDriver() {
    static CudaDriver driver = [] {
        CudaDriver d{};
        void *library = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library) return d;
        bool ok = true;
        const auto load = [&](auto &function, const char *name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
            ok = ok && function;
        };
        load(d.init, "cuInit");
        load(d.deviceGet, "cuDeviceGet");
        load(d.primaryCtxRetain, "cuDevicePrimaryCtxRetain");
        load(d.primaryCtxRelease, "cuDevicePrimaryCtxRelease_v2");
        load(d.primaryCtxSetFlags, "cuDevicePrimaryCtxSetFlags_v2");
        load(d.ctxPush, "cuCtxPushCurrent_v2");
        load(d.ctxPop, "cuCtxPopCurrent_v2");
        load(d.registerImage, "cuGraphicsGLRegisterImage");
        load(d.unregisterResource, "cuGraphicsUnregisterResource");
        load(d.mapResources, "cuGraphicsMapResources");
        load(d.unmapResources, "cuGraphicsUnmapResources");
        load(d.mappedArray, "cuGraphicsSubResourceGetMappedArray");
        load(d.memcpy2DAsync, "cuMemcpy2DAsync_v2");
        d.loaded = ok && d.init(0) == kSuccess;
        return d;
    }();
    return driver.loaded ? &driver : nullptr;
}

// Makes the primary context current for the lifetime of the object.
class ContextScope {
public:
    ContextScope(const CudaDriver *driver, void *context) : driver_(driver) {
        ok_ = driver_->ctxPush(context) == kSuccess;
    }
    ~ContextScope() {
        CUcontext previous = nullptr;
        if (ok_) driver_->ctxPop(&previous);
    }
    bool ok() const { return ok_; }

private:
    const CudaDriver *driver_;
    bool ok_ = false;
};

}  // namespace

CudaGlInterop::~CudaGlInterop() {
    release();
    if (context_) cudaDriver()->primaryCtxRelease(0);
}

bool CudaGlInterop::initialize() {
    if (context_) return true;
    // Interop only works when the OpenGL context runs on the NVIDIA GPU.
    const auto *vendor = reinterpret_cast<const char *>(
        QOpenGLContext::currentContext()->functions()->glGetString(GL_VENDOR));
    if (!vendor || !std::strstr(vendor, "NVIDIA")) return false;

    const CudaDriver *driver = cudaDriver();
    CUdevice device = 0;
    if (!driver || driver->deviceGet(&device, 0) != kSuccess) return false;
    driver->primaryCtxSetFlags(device, kContextBlockingSync);  // fails harmlessly once active
    CUcontext context = nullptr;
    if (driver->primaryCtxRetain(&context, device) != kSuccess) return false;
    context_ = context;
    return true;
}

bool CudaGlInterop::copy(const viplyr::CudaImage &image, const GLuint textures[2]) {
    if (!context_ || image.planeCount != 2) return false;
    const CudaDriver *driver = cudaDriver();
    ContextScope scope(driver, context_);
    if (!scope.ok() || !registerTextures(image, textures)) return false;

    if (driver->mapResources(2, resources_, nullptr) != kSuccess) return false;
    const int bytesPerSample = image.format == viplyr::PixelFormat::P010 ? 2 : 1;
    bool ok = true;
    for (int i = 0; i < 2 && ok; ++i) {
        CUarray array = nullptr;
        ok = driver->mappedArray(&array, resources_[i], 0, 0) == kSuccess;
        if (!ok) break;
        Memcpy2D copy{};
        copy.srcMemoryType = kMemoryDevice;
        copy.srcDevice = image.planes[i];
        copy.srcPitch = static_cast<size_t>(image.pitch[i]);
        copy.dstMemoryType = kMemoryArray;
        copy.dstArray = array;
        copy.WidthInBytes = static_cast<size_t>(image.width[i]) * bytesPerSample * (i == 0 ? 1 : 2);
        copy.Height = static_cast<size_t>(image.height[i]);
        ok = driver->memcpy2DAsync(&copy, nullptr) == kSuccess;
    }
    // Unmapping orders the copies before OpenGL's next use of the textures.
    driver->unmapResources(2, resources_, nullptr);
    return ok;
}

bool CudaGlInterop::registerTextures(const viplyr::CudaImage &image, const GLuint textures[2]) {
    const bool unchanged = resources_[0] && registeredFormat_ == image.format &&
                           registeredWidth_ == image.width[0] && registeredHeight_ == image.height[0] &&
                           registeredTextures_[0] == textures[0] && registeredTextures_[1] == textures[1];
    if (unchanged) return true;
    release();

    const bool sixteenBit = image.format == viplyr::PixelFormat::P010;
    QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
    const CudaDriver *driver = cudaDriver();
    for (int i = 0; i < 2; ++i) {
        const bool luma = i == 0;
        const GLint internalFormat = sixteenBit ? (luma ? GL_R16 : GL_RG16) : (luma ? GL_R8 : GL_RG8);
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, image.width[i], image.height[i], 0,
                         luma ? GL_RED : GL_RG, sixteenBit ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, nullptr);
        if (driver->registerImage(&resources_[i], textures[i], GL_TEXTURE_2D, kRegisterWriteDiscard) != kSuccess) {
            release();
            return false;
        }
    }
    registeredTextures_[0] = textures[0];
    registeredTextures_[1] = textures[1];
    registeredFormat_ = image.format;
    registeredWidth_ = image.width[0];
    registeredHeight_ = image.height[0];
    return true;
}

void CudaGlInterop::release() {
    if (!context_) return;
    const CudaDriver *driver = cudaDriver();
    ContextScope scope(driver, context_);
    for (void *&resource : resources_) {
        if (resource) driver->unregisterResource(resource);
        resource = nullptr;
    }
    registeredFormat_ = viplyr::PixelFormat::Unknown;
    registeredWidth_ = registeredHeight_ = 0;
}

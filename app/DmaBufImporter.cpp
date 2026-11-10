#include "DmaBufImporter.h"

#include <QOpenGLContext>
#include <cstring>
#include <dlfcn.h>

// Keep X11 headers (and their None/Bool macros) out; only EGL types are needed.
#define EGL_NO_X11
#define MESA_EGL_NO_X11_HEADERS
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace {

constexpr uint64_t kInvalidModifier = 0x00ffffffffffffffULL;

bool hasExtension(const char *list, const char *name) {
    if (!list) return false;
    const size_t length = std::strlen(name);
    for (const char *found = std::strstr(list, name); found; found = std::strstr(found + length, name)) {
        const bool startsWord = found == list || found[-1] == ' ';
        const bool endsWord = found[length] == ' ' || found[length] == '\0';
        if (startsWord && endsWord) return true;
    }
    return false;
}

}  // namespace

DmaBufImporter::~DmaBufImporter() { release(); }

bool DmaBufImporter::initialize() {
    available_ = false;
    // libEGL is loaded at run time: Qt has it loaded already when it draws with
    // EGL, and when it draws with GLX there is nothing to import into anyway.
    void *library = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
    const auto getCurrentDisplay = reinterpret_cast<EGLDisplay (*)()>(dlsym(library, "eglGetCurrentDisplay"));
    const auto getCurrentContext = reinterpret_cast<EGLContext (*)()>(dlsym(library, "eglGetCurrentContext"));
    const auto queryString = reinterpret_cast<const char *(*)(EGLDisplay, EGLint)>(dlsym(library, "eglQueryString"));
    const auto getProcAddress = reinterpret_cast<void *(*)(const char *)>(dlsym(library, "eglGetProcAddress"));
    if (!getCurrentDisplay || !getCurrentContext || !queryString || !getProcAddress) return false;

    const EGLDisplay display = getCurrentDisplay();
    if (display == EGL_NO_DISPLAY || getCurrentContext() == EGL_NO_CONTEXT) return false;
    if (!hasExtension(queryString(display, EGL_EXTENSIONS), "EGL_EXT_image_dma_buf_import")) return false;
    if (!QOpenGLContext::currentContext()->hasExtension("GL_OES_EGL_image")) return false;

    imageTargetTexture_ = reinterpret_cast<ImageTargetTexture>(getProcAddress("glEGLImageTargetTexture2DOES"));
    createImage_ = reinterpret_cast<CreateImage>(getProcAddress("eglCreateImageKHR"));
    destroyImage_ = reinterpret_cast<DestroyImage>(getProcAddress("eglDestroyImageKHR"));
    if (!imageTargetTexture_ || !createImage_ || !destroyImage_) return false;

    display_ = display;
    available_ = true;
    return true;
}

bool DmaBufImporter::bind(const viplyr::DmaBufImage &image, const GLuint textures[2]) {
    if (!available_ || image.planeCount != 2) return false;

    void *created[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        const viplyr::DmaBufPlane &plane = image.planes[i];
        EGLint attributes[32];
        int n = 0;
        const auto add = [&](EGLint key, EGLint value) {
            attributes[n++] = key;
            attributes[n++] = value;
        };
        add(EGL_WIDTH, plane.width);
        add(EGL_HEIGHT, plane.height);
        add(EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(plane.drmFormat));
        add(EGL_DMA_BUF_PLANE0_FD_EXT, plane.fd);
        add(EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLint>(plane.offset));
        add(EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(plane.pitch));
        if (plane.modifier != kInvalidModifier) {
            add(EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, static_cast<EGLint>(plane.modifier & 0xffffffffu));
            add(EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, static_cast<EGLint>(plane.modifier >> 32));
        }
        attributes[n] = EGL_NONE;

        created[i] = createImage_(display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes);
        if (!created[i]) {
            destroyImages(created);
            return false;
        }
    }

    QOpenGLFunctions *gl = QOpenGLContext::currentContext()->functions();
    for (int i = 0; i < 2; ++i) {
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        imageTargetTexture_(GL_TEXTURE_2D, created[i]);
    }

    destroyImages(images_);
    images_[0] = created[0];
    images_[1] = created[1];
    boundImage_ = image;
    return true;
}

void DmaBufImporter::release() {
    destroyImages(images_);
    images_[0] = images_[1] = nullptr;
    boundImage_ = viplyr::DmaBufImage();
}

void DmaBufImporter::destroyImages(void *const images[2]) {
    for (int i = 0; i < 2; ++i) {
        if (images[i]) destroyImage_(display_, images[i]);
    }
}

#pragma once
#include <cstdint>
#include <memory>

struct AVFrame;

namespace viplyr {

enum class PixelFormat {
    Unknown,   // a CPU format the renderer does not handle directly; use convert()
    Yuv420p,   // 3 planes: Y, U, V
    Nv12,      // 2 planes: Y, interleaved UV
    Yuv420p10, // 3 planes of 16-bit samples, value in the low 10 bits
    P010,      // 2 planes of 16-bit samples, value in the high 10 bits
    Bgr24,     // 1 plane, matches cv::Mat CV_8UC3
    Rgba,      // 1 plane
    Hardware,  // pixels live on the GPU; use download() to read them
};

// How sample values map to light. Pq and Hlg are the two HDR curves.
enum class TransferFunction { Sdr, Pq, Hlg };

// One plane of a GPU frame shared as a Linux DMA-BUF, ready to import into
// EGL/OpenGL, Vulkan or another device without copying the pixels.
struct DmaBufPlane {
    int fd = -1;              // owned by the DmaBufImage; do not close
    uint32_t drmFormat = 0;   // DRM fourcc of this plane alone (R8, GR88, R16, GR1616)
    uint64_t modifier = 0;    // DRM format modifier (tiling)
    uint32_t offset = 0;
    uint32_t pitch = 0;       // bytes per row
    int width = 0;
    int height = 0;
};

struct DmaBufImage {
    PixelFormat format = PixelFormat::Unknown;  // layout of the planes: Nv12 or P010
    int planeCount = 0;
    DmaBufPlane planes[4];
    std::shared_ptr<void> keepAlive;  // the mapping stays valid while this is held

    bool isValid() const { return planeCount > 0; }
};

// The planes of a CUDA frame: device pointers valid in the device's primary
// context (CUDA device 0), for CUDA/OpenGL interop or GPU inference.
struct CudaImage {
    PixelFormat format = PixelFormat::Unknown;  // Nv12 or P010
    int planeCount = 0;
    uint64_t planes[2] = {0, 0};  // CUdeviceptr
    int pitch[2] = {0, 0};        // bytes per row
    int width[2] = {0, 0};        // in samples (a chroma sample is a U,V pair)
    int height[2] = {0, 0};

    bool isValid() const { return planeCount > 0; }
};

// A decoded video frame. Copying is cheap: copies share the same pixel buffer.
class VideoFrame {
public:
    VideoFrame() = default;

    // Takes ownership of `frame`.
    static VideoFrame adopt(AVFrame *frame, double ptsSeconds, double durationSeconds);

    bool isValid() const { return frame_ != nullptr; }
    int width() const;
    int height() const;
    double pts() const { return pts_; }            // seconds from the start of the media
    double duration() const { return duration_; }  // seconds

    PixelFormat format() const;
    bool isHardware() const { return format() == PixelFormat::Hardware; }
    bool isFullRange() const;  // full (0-255) instead of limited (16-235) range
    bool isBt709() const;      // BT.709 colour matrix instead of BT.601
    bool isBt2020() const;     // BT.2020 wide-gamut colour (used by HDR video)
    TransferFunction transfer() const;

    const uint8_t *plane(int index) const;
    int stride(int index) const;  // bytes per row of the plane

    // Returns a CPU copy of a hardware frame; returns *this for a CPU frame.
    VideoFrame download() const;

    // Shares a VAAPI frame as DMA-BUF planes, without copying. Invalid for any
    // other frame (CPU, CUDA) or when the driver cannot export the surface.
    DmaBufImage exportDmaBuf() const;
    // The device planes of a CUDA frame; invalid for any other frame.
    CudaImage cudaPlanes() const;

    // Converts (and optionally scales) to a CPU format. Zero width/height keeps the size.
    VideoFrame convert(PixelFormat target, int width = 0, int height = 0) const;

    const AVFrame *native() const { return frame_.get(); }

private:
    std::shared_ptr<AVFrame> frame_;
    double pts_ = 0.0;
    double duration_ = 0.0;
};

}  // namespace viplyr

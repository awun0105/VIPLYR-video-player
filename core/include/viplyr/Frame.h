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

    // Converts (and optionally scales) to a CPU format. Zero width/height keeps the size.
    VideoFrame convert(PixelFormat target, int width = 0, int height = 0) const;

    const AVFrame *native() const { return frame_.get(); }

private:
    std::shared_ptr<AVFrame> frame_;
    double pts_ = 0.0;
    double duration_ = 0.0;
};

}  // namespace viplyr

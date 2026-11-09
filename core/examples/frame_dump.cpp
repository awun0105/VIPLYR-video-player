// Example: read frames for computer-vision work.
//
//   frame_dump <video> [seconds]
//
// Seeks to the given time, reads one frame, converts it to BGR24 (the layout of an
// OpenCV cv::Mat of type CV_8UC3) and writes it as a binary PPM image.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "viplyr/FrameReader.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <video> [seconds]\n", argv[0]);
        return 1;
    }
    const double seconds = argc > 2 ? std::atof(argv[2]) : 0.0;

    viplyr::FrameReader reader;
    std::string error;
    if (!reader.open(argv[1], {}, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const viplyr::MediaInfo &info = reader.info();
    std::printf("%dx%d  %.2f fps  %.2f s  codec=%s  decoder=%s\n", info.width, info.height,
                info.frameRate, info.duration, info.videoCodec.c_str(),
                info.hardwareDecoder.empty() ? "cpu" : info.hardwareDecoder.c_str());

    viplyr::VideoFrame frame;
    if (!reader.seek(seconds) || !reader.read(frame)) {
        std::fprintf(stderr, "no frame at %.3f s\n", seconds);
        return 1;
    }

    // With OpenCV:  cv::Mat image(bgr.height(), bgr.width(), CV_8UC3,
    //                             const_cast<uint8_t *>(bgr.plane(0)), bgr.stride(0));
    const viplyr::VideoFrame bgr = frame.convert(viplyr::PixelFormat::Bgr24);

    std::FILE *file = std::fopen("frame.ppm", "wb");
    if (!file) return 1;
    std::fprintf(file, "P6\n%d %d\n255\n", bgr.width(), bgr.height());
    for (int y = 0; y < bgr.height(); ++y) {
        const uint8_t *row = bgr.plane(0) + y * bgr.stride(0);
        for (int x = 0; x < bgr.width(); ++x) {
            const uint8_t rgb[3] = {row[x * 3 + 2], row[x * 3 + 1], row[x * 3]};
            std::fwrite(rgb, 1, 3, file);
        }
    }
    std::fclose(file);
    std::printf("frame at %.3f s written to frame.ppm\n", frame.pts());
    return 0;
}

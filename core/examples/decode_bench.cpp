// Measures how fast this machine decodes a video, per decoder.
//
//   decode_bench <video> [seconds-to-decode]
//
// For each of CPU, VAAPI and CUDA it decodes the start of the file as fast as
// possible and prints frames per second, both with the frames copied to system
// memory (what the player's display path does) and left on the GPU.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "viplyr/FrameReader.h"

namespace {

double wallNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void measure(const char *label, const std::string &path, viplyr::HwAccel hwAccel, bool keepHwFrames,
             double mediaSeconds) {
    viplyr::OpenOptions options;
    options.hwAccel = hwAccel;
    options.keepHwFrames = keepHwFrames;

    viplyr::FrameReader reader;
    if (!reader.open(path, options)) return;
    const bool wantedHardware = hwAccel != viplyr::HwAccel::None;
    if (wantedHardware && reader.info().hardwareDecoder.empty()) {
        std::printf("  %-28s not available for this file\n", label);
        return;
    }

    int frames = 0;
    viplyr::VideoFrame frame;
    const double start = wallNow();
    while (reader.read(frame) && frame.pts() < mediaSeconds) ++frames;
    const double elapsed = wallNow() - start;
    if (frames == 0 || elapsed <= 0.0) return;

    const double framesPerSecond = frames / elapsed;
    std::printf("  %-28s %7.1f fps   (%.1fx real time)\n", label, framesPerSecond,
                framesPerSecond / reader.info().frameRate);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <video> [seconds-to-decode]\n", argv[0]);
        return 1;
    }
    const std::string path = argv[1];
    const double mediaSeconds = argc > 2 ? std::atof(argv[2]) : 10.0;

    viplyr::FrameReader probe;
    std::string error;
    if (!probe.open(path, {}, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const viplyr::MediaInfo info = probe.info();
    probe.close();
    std::printf("%s  %dx%d  %.2f fps  %s\n", path.c_str(), info.width, info.height, info.frameRate,
                info.videoCodec.c_str());

    measure("cpu", path, viplyr::HwAccel::None, false, mediaSeconds);
    measure("vaapi, copied to memory", path, viplyr::HwAccel::Vaapi, false, mediaSeconds);
    measure("vaapi, kept on gpu", path, viplyr::HwAccel::Vaapi, true, mediaSeconds);
    measure("cuda, copied to memory", path, viplyr::HwAccel::Cuda, false, mediaSeconds);
    measure("cuda, kept on gpu", path, viplyr::HwAccel::Cuda, true, mediaSeconds);
    return 0;
}

# Using `viplyr_core` in another project

`viplyr_core` is the playback engine without any UI. It needs C++17 and FFmpeg 6.1+.

## Build and link

```cmake
set(VIPLYR_BUILD_APP OFF)      # no Qt needed
set(VIPLYR_BUILD_TESTS OFF)
add_subdirectory(third_party/viplyr)
target_link_libraries(my_target PRIVATE viplyr_core)
```

## Two ways to get frames

| Need | Use |
|---|---|
| Process a file as fast as possible, frame by frame | `FrameReader` |
| Real-time playback with sound, or several consumers at once | `Player` + `IVideoSink` |

### `FrameReader`: pull frames yourself

```cpp
#include "viplyr/FrameReader.h"

viplyr::FrameReader reader;
std::string error;
if (!reader.open("run_042.mp4", {}, &error)) { /* report error */ }

reader.seek(12.5);                       // frame-accurate
viplyr::VideoFrame frame;
while (reader.read(frame)) {
    viplyr::VideoFrame bgr = frame.convert(viplyr::PixelFormat::Bgr24);
    cv::Mat image(bgr.height(), bgr.width(), CV_8UC3,
                  const_cast<uint8_t *>(bgr.plane(0)), bgr.stride(0));
    // `image` points into `bgr`; keep `bgr` alive while you use it.
    process(image, frame.pts());         // pts is in seconds
}
```

`core/examples/frame_dump.cpp` is a complete program built on this.

### `Player`: frames pushed at presentation time

```cpp
#include "viplyr/Player.h"

class DetectorSink : public viplyr::IVideoSink {
public:
    // Called on an engine thread, once per frame, at the moment it should be shown.
    // Return quickly: hand the frame to your own thread or queue. Copying a
    // VideoFrame is cheap (the pixels are shared).
    void present(const viplyr::VideoFrame &frame) override { queue_.push(frame); }
};

DetectorSink detector;
viplyr::Player player;
player.addVideoSink(&detector);          // add as many sinks as you need
player.open("rtsp://camera.local/stream", {});
player.play();
```

`open()` accepts anything libavformat can read: files, `rtsp://`, `http://`, and so on.
Low-latency settings go through `OpenOptions::formatOptions`, for example
`{{"rtsp_transport", "tcp"}, {"fflags", "nobuffer"}}`.

## CPU and GPU

`OpenOptions::hwAccel` selects the decoder: `Auto`, `None`, `Vaapi` or `Cuda`.
`MediaInfo::hardwareDecoder` tells you what was actually used.

`Auto` picks what is fastest for how the frames are used: the CPU when frames are copied to
system memory (the default), the GPU when `keepHwFrames` is set. The reason is the read-back
cost. Measured with `decode_bench` on the development laptop (i7 + UHD 630 + Quadro T2000),
4K 60 fps:

| Decoder | H.264 | HEVC 10-bit |
|---|---|---|
| CPU | 296 fps | 154 fps |
| VAAPI, copied to memory | 48 fps | 25 fps |
| VAAPI, kept on GPU | 315 fps | 229 fps |
| CUDA, copied to memory | 76 fps | 114 fps |
| CUDA, kept on GPU | 159 fps | 310 fps |

These are synthetic clips; run `decode_bench <your video>` for your own material.

By default a frame decoded on the GPU is copied to system memory, so every sink can read it.
Set `OpenOptions::keepHwFrames = true` to leave frames on the GPU: `frame.isHardware()` is
then true, `frame.native()` gives the underlying `AVFrame` (VAAPI surface or CUDA frame) for
zero-copy consumers, and `frame.download()` returns a CPU copy when one is needed.

## Tracks, subtitles and special formats

- `MediaInfo::audioTracks` / `subtitleTracks` list what the file contains; play another audio
  track with `OpenOptions::audioStreamIndex`.
- `readSubtitleTrack(url, streamIndex)` (`viplyr/SubtitleReader.h`) returns the cues of a text
  subtitle track. It scans the whole file, so call it from a worker thread.
- Interlaced video is deinterlaced automatically (on the CPU). `MediaInfo::pixelAspectRatio`
  tells you the shape of one pixel. 10-bit frames arrive as `Yuv420p10` or `P010`;
  `VideoFrame::transfer()` reports HDR (PQ/HLG). `convert()` to `Bgr24` does not tone-map.

## Overlays in the desktop player

The player draws annotations from a JSON file next to the video, named
`<video name>.overlay.json`. Coordinates are in video pixels, `t` in seconds, and an entry
stays on screen until the next entry's time:

```json
{ "frames": [
  { "t": 1.25,
    "boxes":  [ { "x": 10, "y": 20, "w": 100, "h": 50, "label": "car 0.92", "color": "#ff0000" } ],
    "points": [ { "x": 60, "y": 45, "label": "centre" } ] }
] }
```

Press `I` in the player for the frame number and exact timestamp of the frame on screen.

## ROS 2

There is no ROS code in this repository yet. A publisher is an `IVideoSink` that lives in
your ROS package:

```cpp
class ImagePublisherSink : public viplyr::IVideoSink {
public:
    explicit ImagePublisherSink(rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher)
        : publisher_(std::move(publisher)) {}

    void present(const viplyr::VideoFrame &frame) override {
        const viplyr::VideoFrame bgr = frame.convert(viplyr::PixelFormat::Bgr24);
        sensor_msgs::msg::Image message;
        message.height = bgr.height();
        message.width = bgr.width();
        message.encoding = "bgr8";
        message.step = bgr.stride(0);
        message.data.assign(bgr.plane(0), bgr.plane(0) + bgr.stride(0) * bgr.height());
        publisher_->publish(message);
    }

private:
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
};
```

This sketch has not been compiled or run; treat it as a starting point.

## Threading rules

- Call a `Player`'s methods from one thread.
- `present()` and the `onEnded` / `onError` / `onStateChanged` callbacks run on engine
  threads. Do not call `open()` or `close()` from inside them.
- A `FrameReader` has no threads of its own; use one reader per thread.

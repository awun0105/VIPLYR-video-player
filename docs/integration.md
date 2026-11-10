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

Two helpers hand GPU frames on without a copy:

- `frame.exportDmaBuf()` (VAAPI): the luma and chroma planes as DMA-BUF file descriptors with
  DRM format, modifier, offset and pitch, ready for `EGL_EXT_image_dma_buf_import`, Vulkan or
  another device. Keep the returned `DmaBufImage` alive while the planes are in use.
- `frame.cudaPlanes()` (CUDA): device pointers and pitches of the planes. CUDA decoding uses the
  device's primary context, so code in the same process (TensorRT, a CUDA kernel, an OpenGL
  interop) can read them directly; retain it with `cuDevicePrimaryCtxRetain(0)` after setting
  `CU_CTX_SCHED_BLOCKING_SYNC`.

The Qt app uses both (`app/DmaBufImporter`, `app/CudaGlInterop`): the measured CPU load for a
4K 60 fps clip falls from about 100% (H.264) / 250% (HEVC 10-bit) of a core to 8–15%.

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

## Cameras and live streams

`OpenOptions::inputFormat` opens a device: `"v4l2"` for cameras (chosen automatically for
`/dev/video*`), `"x11grab"` for the screen, `"lavfi"` for FFmpeg test sources. Device settings
go in `formatOptions`, e.g. `{"video_size", "1280x720"}, {"framerate", "30"},
{"input_format", "mjpeg"}`.

`OpenOptions::lowLatency = true` is for live sources. It turns off input buffering and
frame-threaded decoding, which holds several frames back, and the player shows each frame as
soon as it is decoded. When frames queue up, it skips to the newest. `position()` follows the
newest frame shown.

## Recording

`viplyr::VideoRecorder` is an `IVideoSink` that encodes what it receives into a file (.mp4,
.mkv, ...). Encoding runs on its own thread, so `present()` never holds up playback. When
the encoder falls behind, frames are dropped and counted (`droppedFrames()`), instead of
slowing the player down. `encoder = "auto"` uses NVENC when there is one, and libx264
otherwise.

```cpp
viplyr::VideoRecorder recorder;
player.addVideoSink(&recorder);
recorder.start("run_042.mp4");      // records from the next frame on
...
std::string error;
if (!recorder.stop(&error)) std::cerr << error << '\n';
```

## Python

`python/` builds a `viplyr` module with pybind11. You need `pybind11-dev` and `python3-dev`,
and the build output is `build/python/viplyr.*.so`. Images reach NumPy without a copy: each
array keeps its frame alive.

```python
import viplyr

for image, pts in viplyr.FrameReader("video.mp4"):       # image: (H, W, 3) uint8 BGR
    ...

reader = viplyr.FrameReader("video.mp4", hw="cuda", keep_gpu=True)
frame = reader.read_frame()
luma = cupy.asarray(frame.luma)                          # or torch.as_tensor(frame.luma, device="cuda")
chroma = cupy.asarray(frame.chroma)                      # (H/2, W/2, 2) interleaved U, V

player = viplyr.Player()
player.on_frame = lambda frame: detector(frame.to_numpy("rgb"))   # called on an engine thread
player.open("/dev/video0", low_latency=True, format_options={"video_size": "1280x720"})
player.start_recording("session.mp4")
player.play()
```

`frame.luma` and `frame.chroma` expose `__cuda_array_interface__`, so PyTorch, CuPy and
Numba read the decoder's GPU memory in place. CUDA decoding uses the device's primary
context, which is the context PyTorch uses. `viplyr.set_log_level("info")` brings back
FFmpeg's messages; the module sets them to `"warning"` on import.

## ROS 2

`ros2/viplyr_ros` is an ament package with one node, `video_publisher`. It publishes a file,
stream or camera as `sensor_msgs/Image` (`image_raw`) plus `CameraInfo` (`camera_info`, size
only). Put the repository, or a link to it, in your workspace's `src` and build with colcon.
The package builds `core/` itself, so it needs only the FFmpeg development packages
(`libav*-dev`, including `libavdevice-dev`).

```bash
ros2 launch viplyr_ros video_publisher.launch.py url:=/data/run.mp4 hw:=auto
ros2 run viplyr_ros video_publisher --ros-args -p url:=/dev/video0 -p low_latency:=true \
    -p format_options:="['video_size=1280x720','framerate=30']"
ros2 service call /viplyr_video_publisher/set_playing std_srvs/srv/SetBool "{data: false}"
ros2 topic pub --once /viplyr_video_publisher/seek std_msgs/msg/Float64 "{data: 12.5}"
ros2 service call /viplyr_video_publisher/set_recording std_srvs/srv/SetBool "{data: true}"  # needs record_path
```

The parameters are listed at the top of `src/video_publisher.cpp`. Images are converted and
published on a thread of their own, so a slow subscriber cannot slow down playback. When
publishing falls behind, the node warns every 10 s with the number of frames it skipped.

Large images between processes: with the default DDS over UDP, best-effort images of
640×360 and larger sometimes lose frames on the way. This was measured in the `ros:jazzy`
container with a Python subscriber, while the node itself published every frame. For full
rate, run the node as a component (`viplyr_ros::VideoPublisher`) in the same container as
its subscribers with intra-process communication on. Otherwise, use a shared-memory DDS
transport, or `reliable:=true`. `test/check_publisher.py` and `test/check_live.sh` are the
checks that were run against it.

## Threading rules

- Call a `Player`'s methods from one thread.
- `present()` and the `onEnded` / `onError` / `onStateChanged` callbacks run on engine
  threads. Do not call `open()` or `close()` from inside them.
- A `FrameReader` has no threads of its own; use one reader per thread.

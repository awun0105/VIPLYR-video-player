# Viplyr - A Simple & Powerful Video Player for Linux

**Viplyr** is an open-source video player built with **C++17**, **FFmpeg** and **Qt6**. 
It is a lightweight yet feature-rich application optimized for **Linux** (Ubuntu, Debian, etc.), built around its own playback engine (`viplyr_core`) that can be reused without the UI in robotics, simulation and computer-vision projects.

---

## 1. Project Folder Structure

```text
Viplyr/
├── CMakeLists.txt           # Top level: builds core/, and app/ unless VIPLYR_BUILD_APP=OFF
├── Makefile, setup.sh       # Convenience build/run/test targets
├── docs/                    # Design notes and the integration guide
│
├── core/                    # viplyr_core: the playback engine (C++17 + FFmpeg, no Qt)
│   ├── include/viplyr/      # Public API
│   │   ├── Player.h         # Play, pause, seek, step, rate; audio/video sync
│   │   ├── FrameReader.h    # Read frames one by one (batch work, thumbnails)
│   │   ├── Frame.h          # VideoFrame: pixels, timestamp, conversion to BGR/RGBA
│   │   ├── Sinks.h          # IVideoSink / IAudioSink extension points
│   │   ├── MediaInfo.h      # MediaInfo, OpenOptions, HwAccel, tracks
│   │   ├── SubtitleReader.h # Read a text subtitle track from a file
│   │   ├── VideoRecorder.h  # Encode frames to a file on its own thread
│   │   ├── Logging.h        # FFmpeg log level
│   │   └── Playlist.h
│   ├── src/                 # Pipeline threads, clock, queues, FFmpeg helpers
│   ├── tests/               # Headless tests (ctest)
│   └── examples/            # frame_dump (frame as BGR image), decode_bench (decoder speed)
│
├── python/                  # `import viplyr`: pybind11 module and its tests
├── ros2/viplyr_ros/         # ROS 2 package: video_publisher node / component
│
└── app/                     # The Qt6 desktop player, a thin layer over the core
    ├── PlayerController     # Playback model the UI talks to
    ├── VideoWidget          # OpenGL video output (zoom, pan, rotate, subtitles)
    ├── DmaBufImporter, CudaGlInterop  # GPU frames to OpenGL textures without a copy
    ├── AudioOutput          # Audio device output
    ├── ThumbnailProvider    # Seek-bar previews on a worker thread
    ├── SubtitleTrack        # .srt and embedded subtitle lookup
    ├── OverlayTrack         # Boxes, points and labels drawn over the video
    ├── ExportCommand/Dialog # Trim, crop, rotate, sound; runs ffmpeg
    └── MainWindow, ControlBar, PlaylistPanel, StartPanel, ...
```

---

## 2. Key Features

### ✨ Playback
- **Drag & Drop**: Load files or entire folders; folders are scanned on a worker thread.
- **Frame-Accurate Seeking & Stepping**: Seek to an exact frame, step frame by frame, jump to frame N.
- **Playback Speed**: 0.5x to 2.0x without changing the pitch.
- **Resume**: Each video continues where it was left.
- **Audio Tracks & Subtitles**: Choose the audio track; subtitles from a `.srt` file next to the video or from a track inside it: text, styled ASS/SSA (fonts, colours, positions, through libass) and picture subtitles (DVD, Blu-ray PGS, DVB). 5.1 and 7.1 sound reach a surround device channel by channel.
- **Chapters, A-B Loop, Picture Controls**: Chapter marks on the seek bar and in the preview; loop a passage; brightness, contrast, gamma and saturation.
- **Formats**: Anything the system FFmpeg decodes. Interlaced video is deinterlaced, non-square pixels are shown at the right shape, 10-bit and HDR (PQ/HLG) video is tone-mapped for a normal screen.
- **Network Streams**: Open `rtsp://`, `http://` and similar addresses.

### 🖱️ Interface
- **Overlay Controls**: The control bar floats over the video and fades away during playback, together with the cursor.
- **Thumbnail Preview**: Hover over the seek bar to preview the video.
- **Playlist**: Preview picture and length for each item, search, drag to reorder.
- **On-Screen Messages**: Short confirmations for volume, speed, seeking and saved files.
- **Zoom, Pan, Rotate, Always on Top**, and the desktop stays awake while a video plays.

### ✂️ Edit & Export
- **Timeline editor** (`E` or `Ctrl+E`): filmstrip thumbnails, yellow trim handles, a draggable playhead and split (`B`), duplicate/delete, drag clips to reorder, and drop local videos between clips to insert them. One video track with each clip's original sound; edits leave source files untouched.
- **Undo/Redo and projects**: `Ctrl+Z` / `Ctrl+Shift+Z`, save with `Ctrl+S` to `.viplyr.json` and reopen with `Ctrl+O`. Projects reference the original source paths; keep those files available.
- **Multi-source preview and export**: play the timeline in order, including cuts from different files. Export fits different frame sizes into a chosen canvas (original, landscape, portrait or square), uses the first clip's frame rate up to 60 fps, and inserts silence for clips without audio. Source changes during preview reopen the decoder and can briefly pause at a boundary; export renders the joined timeline in one pass.
- **Trim**, **crop** (16:9, 9:16, 1:1, 4:3 or custom), **rotate**, **remove or replace the sound**.
- **Cut out several parts and join them**: list the parts, see them on the seek bar, preview them back to back, export them as one video.
- Trimming and sound changes are exported without re-encoding (fast, no quality loss); crop, rotation, joins and frame-exact cuts re-encode, on the CPU (x264) or on the GPU (NVENC, VAAPI) when the machine has one.
- Exports run in the background with their progress shown over the video, so playback goes on meanwhile.
- Export a range as a **PNG image sequence**, or save the current frame as an image.

### 🤖 For Robotics & Computer Vision
- **Overlays**: Draw boxes, points and labels over the video from a `<video name>.overlay.json` file (see [docs/integration.md](docs/integration.md)).
- **Frame Information**: Frame number, exact timestamp, resolution, codec and decoder.
- **Reusable Engine**: `viplyr_core` delivers frames to your own code; `decode_bench` measures decoding speed per decoder.
- **Cameras and Live Streams**: V4L2 cameras, screen capture and RTSP with a low-latency mode.
- **Recording**: `VideoRecorder` encodes any frame stream to a file (NVENC or libx264) without slowing playback.
- **Python**: `import viplyr` gives frames as NumPy arrays without a copy, and CUDA frames to PyTorch/CuPy in place.
- **ROS 2**: the `viplyr_ros` node publishes files, streams and cameras as `sensor_msgs/Image`, with play, pause, seek and record control.

### ⌨️ Controls (Hotkeys)

| Key | Action |
| --- | --- |
| `Space` | Play / Pause |
| `Left` / `Right` | Seek backward / forward 5s |
| `,` / `.` | Step one frame back / forward |
| `Ctrl+G` | Go to frame number |
| `PgUp` / `PgDn` | Previous / next chapter |
| `L` | A-B loop: set A, set B, clear |
| `1`/`2` `3`/`4` `5`/`6` `7`/`8` | Contrast, brightness, gamma, saturation down / up; `0` resets |
| `Up` / `Down` | Volume |
| `M` | Mute / Unmute |
| `F` / `Double Click` / `Esc` | Fullscreen on, off |
| `P` | Show / hide playlist |
| `I` | Show / hide frame information |
| `S` | Save the current frame as PNG |
| `E` / `Ctrl+E` | Open timeline editor |
| `Shift+E` | Quick trim/crop/rotate export of the player video |
| `R` | Rotate Video 90° |
| `T` | Toggle Always on Top |
| `Ctrl+O` / `Ctrl+U` | Add files / open a network URL |
| `Mouse Wheel` / `Left Click Drag` | Zoom / pan when zoomed |

---

## 3. Project Architecture

Viplyr is split into two parts with a one-way dependency: the app uses the core, the core knows nothing about the app.

- **`viplyr_core`** — a pipeline of four threads: demux → decode (video and audio) → present. An audio-driven clock keeps picture and sound in sync. Decoded frames are delivered to any number of `IVideoSink`s, so a display, a computer-vision stage and a recorder can all receive the same frames.
- **`app`** — `PlayerController` wraps the engine for the UI (signals, playlist, subtitles); `MainWindow` only builds widgets and connects them.

To use the engine in another project, build with `-DVIPLYR_BUILD_APP=OFF` and link `viplyr_core`. See [docs/integration.md](docs/integration.md).

---

## 4. How to Build and Run

### Prerequisites
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
                 qt6-base-dev qt6-multimedia-dev \
                 libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavfilter-dev libavdevice-dev libass-dev libegl-dev
# optional, for `import viplyr`: pybind11-dev python3-dev
```

Qt 6.4 or newer and FFmpeg 6.1 or newer are required. `make test` runs the engine's headless tests.

With an audio server and output device available, run `./build/app/audio_output_tests`
to check that audio continues while the GUI event loop is blocked, plus pause, flush
and shutdown. It mutes its own stream and leaves the system volume unchanged.
Pass `build/core/test_clip.mp4` as an argument to also check playback, pause, seek,
speed changes and end-of-stream through the real audio backend.
`make test` also checks non-destructive timeline operations and real mixed-source exports.
With a display, `./build/app/editor_ui_tests build/core/test_clip.mp4 build/core/test_tracks.mkv`
checks trim handles, split/undo, file drops, project round trips and preview order.

When the screen can take GPU frames directly, the player decodes on the GPU and draws each frame without copying it through system memory: VAAPI frames are imported as DMA-BUF through EGL (Intel/AMD), NVDEC frames are copied GPU-to-GPU through CUDA/OpenGL interop (when OpenGL runs on the NVIDIA GPU). 4K 60 fps then takes about 10–15% of one CPU core instead of 1–2.5 cores. Otherwise it decodes on the CPU, which beats decoding on the GPU and reading the frames back. The start-up line `Viplyr: GPU frames shown without a CPU copy: ...` tells which path is in use. `VIPLYR_HWACCEL=none|vaapi|cuda` forces a decoder (frames then go through system memory). Editing needs the `ffmpeg` program.

### Install a release (Ubuntu 24.04)
Download `viplyr_<version>_amd64.deb` from [Releases](https://github.com/awun0105/VIPLYR-video-player/releases), then:
```bash
sudo apt install ./viplyr_*_amd64.deb
```
This installs `Viplyr`, its menu entry and icon, and pulls in Qt 6, FFmpeg and libass.

### Build from source
```bash
git clone https://github.com/awun0105/VIPLYR-video-player.git
cd VIPLYR-video-player
./setup.sh
```
Install it with `sudo cmake --install build` (binary, menu entry, icon under `/usr/local`),
or build packages with `cd build && cpack` (`.deb` and `.tar.gz`).

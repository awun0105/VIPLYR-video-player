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
│   │   ├── MediaInfo.h      # MediaInfo, OpenOptions, HwAccel
│   │   └── Playlist.h
│   ├── src/                 # Pipeline threads, clock, queues, FFmpeg helpers
│   ├── tests/               # Headless tests (ctest)
│   └── examples/            # frame_dump: a frame as a BGR image
│
└── app/                     # The Qt6 desktop player, a thin layer over the core
    ├── PlayerController     # Playback model the UI talks to
    ├── VideoWidget          # OpenGL video output (zoom, pan, rotate, subtitles)
    ├── AudioOutput          # Audio device output
    ├── ThumbnailProvider    # Seek-bar previews on a worker thread
    ├── SubtitleTrack        # .srt loading and lookup
    └── MainWindow, ControlBar, PlaylistPanel, StartPanel, ...
```

---

## 2. Key Features

### ✨ Advanced Playback
- **Drag & Drop**: Seamlessly load files or entire directories.
- **Asynchronous Loading**: Process thousands of files without freezing the UI.
- **Playback Speed**: Adjust speed from **0.5x to 2.0x**.
- **Subtitles**: Automatic detection and real-time sync for **.srt** files (Binary Search optimized).
- **Hardware Decoding**: VAAPI and NVDEC/CUDA, with automatic fallback to the CPU.
- **Frame-Accurate Seeking & Stepping**: Seek to an exact frame and step frame by frame.

### 🖱️ Interactive UI
- **Thumbnail Preview**: Hover over the seek bar to see video frames (cached; only the latest request is decoded).
- **Jump-to-Click**: Instantly seek time or volume by clicking anywhere on the sliders.
- **Playlist Search**: Real-time filtering for large video collections.
- **Auto-hide Controls**: UI and cursor automatically hide during inactivity for immersive viewing.
- **Playback Idle Inhibit**: Keeps the desktop session awake while video is playing.

### 🖼️ Visuals & Transformations
- **Zoom & Pan**: Mouse wheel to zoom, left-click drag to pan.
- **Rotate**: 90-degree rotation support.
- **Always on Top**: Toggle "Stay on Top" mode to multitask while watching.

### ⌨️ Controls (Hotkeys)

| Key | Action |
| --- | --- |
| `Space` | Play / Pause |
| `F` | Toggle Fullscreen |
| `M` | Mute / Unmute |
| `T` | Toggle Always on Top |
| `R` | Rotate Video 90° |
| `Left` / `Right` | Seek backward / forward 5s |
| `,` / `.` | Step one frame back / forward |
| `Up` / `Down` | Increase / Decrease Volume |
| `Esc` | Exit Fullscreen |
| `Double Click` | Toggle Fullscreen |
| `Mouse Wheel` | Zoom In / Out |
| `Left Click Drag` | Pan Video (when zoomed) |

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
                 libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavfilter-dev
```

Qt 6.4 or newer and FFmpeg 6.1 or newer are required. `make test` runs the engine's headless tests.

Set `VIPLYR_HWACCEL=none` (or `vaapi`, `cuda`) to override the automatic choice of decoder.

### Build & Install
1. **Clone & Build**:
```bash
git clone https://github.com/awun0105/VIPLYR-video-player.git
cd VIPLYR-video-player
./setup.sh
```

2. **System Integration (Recommended)**:
```bash
# Install binary
sudo cp build/Viplyr /usr/local/bin/Viplyr

# Install icon
mkdir -p ~/.local/share/icons
cp icon/viplyr_icon.png ~/.local/share/icons/viplyr_icon.png

# Create desktop entry for "Open With..." support
cat > ~/.local/share/applications/Viplyr.desktop << EOF
[Desktop Entry]
Name=Viplyr
Exec=/usr/local/bin/Viplyr %f
Icon=/home/$USER/.local/share/icons/viplyr_icon.png
Type=Application
Categories=AudioVideo;Player;Video;
Terminal=false
MimeType=video/mp4;video/x-matroska;video/webm;video/quicktime;video/x-msvideo;video/x-flv;video/mpeg;video/ogg;video/3gpp;
EOF

update-desktop-database ~/.local/share/applications/
```

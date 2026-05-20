# Viplyr - A Simple & Powerful Video Player for Linux

**Viplyr** is an open-source video player built with **C++11** and **Qt5**. 
It is a lightweight yet feature-rich application optimized for **Linux** (Ubuntu, Debian, etc.), featuring a clean MVC architecture and high-performance background processing.

---

## 1. Project Folder Structure

The project maintains a strict separation between **UI**, **Core Logic**, and **Utilities**:

```text
Viplyr/
├── Makefile                 # Convenience targets: build/run/setup/clean
├── CMakeLists.txt           # CMake configuration with QtConcurrent support
├── setup.sh                 # Clean + configure (CMake) + build (Ninja)
│
├── icon/
│   └── viplyr_icon.png      # Application icon
│
└── src/
    ├── main.cpp             # Program entry point
    │
    ├── Utils/               # Utility & Background Logic
    │   ├── Theme.h          # UI styles (CSS)
    │   ├── Constants.h      # Global constants & time formatting
    │   ├── SubtitleParser.h # SRT file parsing
    │   └── ThumbnailExtractor.h # Async frame capture
    │
    ├── Core/                # Core logic layer (Encapsulated)
    │   ├── MediaEngine.h
    │   └── MediaEngine.cpp  # Video playback, playlist, & sync logic
    │
    └── UI/                  # User Interface layer
        ├── Components.h/.cpp    # Custom Sliders, Control Bar, Panels
        ├── VideoCanvas.h/.cpp   # Video display (Zoom, Pan, Rotate, Subtitles)
        ├── ThumbnailPopup.h/.cpp # Preview popup component
        └── MainWindow.h/.cpp    # Main window orchestrator
```

---

## 2. Key Features

- **Advanced Playback**:
    - **Drag & Drop**: Seamlessly load files or entire directories.
    - **Asynchronous Loading**: Process thousands of files without freezing the UI.
    - **Playback Speed**: Adjust speed from **0.5x to 2.0x**.
    - **Subtitles**: Automatic detection and real-time sync for **.srt** files (Binary Search optimized).
- **Interactive UI**:
    - **Thumbnail Preview**: Hover over the seek bar to see video frames (Cached & Throttled).
    - **Jump-to-Click**: Instantly seek time or volume by clicking anywhere on the sliders.
    - **Playlist Search**: Real-time filtering for large video collections.
    - **Auto-hide Controls**: UI and cursor automatically hide during inactivity for immersive viewing.
- **Visuals & Transformations**:
    - **Zoom & Pan**: Mouse wheel to zoom, left-click drag to pan.
    - **Rotate**: 90-degree rotation support.
    - **Always on Top**: Toggle "Stay on Top" mode to multitask while watching.

---

## 3. Project Architecture

Viplyr follows a **Strict MVC (Model-View-Controller)** pattern:
- **Core (Model)**: `MediaEngine` encapsulates `QMediaPlayer` and `QMediaPlaylist`. It handles all state and emits signals.
- **UI (View)**: Dedicated widgets for canvas, controls, and popups.
- **MainWindow (Controller)**: Centralizes all signal/slot connections and manages high-level application flow.

**Performance Note**: Heavy tasks (file filtering, thumbnail extraction) are offloaded to worker threads using `QtConcurrent`, ensuring a 60FPS responsive interface.

---

## 4. How to Build and Run

### Prerequisites
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
                 qtbase5-dev qtmultimedia5-dev libqt5multimediawidgets5
```

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

---

## 5. Controls (Hotkeys)

| Key | Action |
| --- | --- |
| `Space` | Play / Pause |
| `F` | Toggle Fullscreen |
| `M` | Mute / Unmute |
| `T` | Toggle Always on Top |
| `R` | Rotate Video 90° |
| `Left` / `Right` | Seek backward / forward 5s |
| `Up` / `Down` | Increase / Decrease Volume |
| `Esc` | Exit Fullscreen |
| `Double Click` | Toggle Fullscreen |
| `Mouse Wheel` | Zoom In / Out |
| `Left Click Drag` | Pan Video (when zoomed) |

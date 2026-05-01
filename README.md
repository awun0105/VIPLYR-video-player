# Viplyr - A Simple Video Player for Linux

**Viplyr** is an open-source video player built with **C++11** and **Qt5**.
It is currently optimized for **Linux** (especially Ubuntu and Debian-based distributions).

The goal of this project is to create a lightweight, easy-to-use video player with useful features such as drag-and-drop support, zoom & pan, and video rotation.

---

## 1. Project Folder Structure

The project is well-organized by strictly separating the **User Interface (UI)** from the **Core Logic**:

```text
Viplyr/
├── Makefile                 # Convenience targets: build/run/setup/clean
├── CMakeLists.txt           # CMake configuration file
├── setup.sh                 # Clean + configure (CMake) + build (Ninja)
│
├── icon/
│   └── viplyr_icon.png      # Application icon
│
└── src/
    ├── main.cpp             # Program entry point
    │
    ├── Utils/
    │   └── Theme.h          # UI colors and styles (CSS)
    │
    ├── Core/                # Core logic layer (no UI code)
    │   ├── MediaEngine.h
    │   └── MediaEngine.cpp  # Handles video playback and playlist
    │
    └── UI/                  # User Interface layer
        ├── Components.h/.cpp    # Reusable UI blocks (buttons, control bar, start screen)
        ├── VideoCanvas.h/.cpp   # Video display area (supports zoom, pan, rotate)
        └── MainWindow.h/.cpp    # Main window that coordinates everything
```

---

## 2. Project Architecture (Simple Explanation)

Viplyr follows the **MVC (Model-View-Controller)** pattern combined with the **Facade Pattern**.

- **Core (Model)**: This part contains all the video playback logic. It uses Qt’s `QMediaPlayer` and `QMediaPlaylist`. It **does not know anything** about buttons, colors, or the user interface.
- **UI (View)**: This part is responsible only for displaying things and handling user interactions (drag & drop, button clicks, zoom, etc.). It **does not know** how video decoding works.
- **MainWindow (Controller)**: This acts as the “conductor”. It creates all the components, arranges the layout, and connects the UI and Core together using Qt’s **Signals & Slots** mechanism.

All connections between UI and Core are centralized in one function called `wireConnections()` inside `MainWindow`. This keeps the code clean and avoids messy “spaghetti code”.

### Benefits of this design:
- Easy to maintain and modify.
- You can change the interface without touching the video playback code (and vice versa).
- The code stays organized even as the project grows.

---

## 3. Key Features

- **Drag & Drop**: Simply drag video files (or multiple files) and drop them into the player window to start playing.
- **Playback Controls**: Play/Pause, Stop, Next/Prev, and skip ±5 seconds.
- **Smart Seeking**: Click anywhere on the timeline to jump to that exact time (no need to drag the slider).
- **Real-time Duration**: Shows current time and total duration in `00:00:00 / 01:23:45` format.
- **Spatial Transformations**:
  - **Zoom & Pan**: Scroll the mouse wheel to zoom in/out, hold left-click and drag to pan the video.
  - **Rotate**: Press the **R** key to rotate the video 90 degrees.
- **Fullscreen**: Double click on the video to toggle fullscreen. Press **Esc** to exit fullscreen.
- **Playlist**:
  - Drop multiple files to build a playlist and auto-play sequentially.
  - Add more videos later via the playlist **Add** button or drag-and-drop into the playlist panel (duplicates are ignored).
  - Remove the selected item with **Remove**.
  - Hide/show the playlist from the control bar.

---

## 4. Main Components and How They Work

1. **MainWindow** (Controller)
   The central coordinator. It initializes all objects, sets up the layout, and connects everything together.

2. **MediaEngine** (Core Logic)
   A simple facade that hides the complexity of Qt Multimedia. It provides easy-to-use functions like `play()`, `pause()`, `seekTo()`, and `setVolume()`.

3. **UI Components**:
   - **StartPanel**: The welcome screen that supports drag-and-drop.
   - **VideoCanvas**: The actual area where the video is displayed. It handles zoom, pan, and rotation using `QGraphicsView`.
   - **ControlBar**: The bottom bar containing play/pause buttons, seek slider, volume slider, and time labels.

**Basic Flow**:
User drags a file → StartPanel receives it → MainWindow passes it to MediaEngine → MediaEngine plays the video → VideoCanvas shows the picture → ControlBar updates buttons and time.

---

## 5. How to Build and Run

### Prerequisites

You need a Linux system (Ubuntu or Debian recommended). Install the required packages:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
                 qtbase5-dev qtmultimedia5-dev libqt5multimediawidgets5
```

### Build the Project

1. Clone the repository:
```bash
git clone https://github.com/awun0105/VIPLYR-video-player.git
cd VIPLYR-video-player
```

2. Configure and build:
```bash
chmod +x setup.sh
./setup.sh
```

After a successful build, the executable will be located at `build/Viplyr`.

For incremental rebuilds after editing code:
```bash
make build
```

### How to Launch Viplyr

**Method 1: Run directly from terminal (for development)**
```bash
./build/Viplyr
```

Or use:
```bash
make run
```

**Method 2: Install as a regular desktop application** (recommended for daily use)

```bash
# Copy the executable
sudo cp build/Viplyr /usr/local/bin/Viplyr

# Copy the icon
mkdir -p ~/.local/share/icons
cp icon/viplyr_icon.png ~/.local/share/icons/viplyr_icon.png

# Create desktop entry
mkdir -p ~/.local/share/applications
cat > ~/.local/share/applications/Viplyr.desktop << EOF
[Desktop Entry]
Name=Viplyr
Exec=/usr/local/bin/Viplyr
Icon=viplyr_icon
Type=Application
Categories=AudioVideo;Player;Video;
MimeType=video/mp4;video/x-matroska;video/webm;video/quicktime;
EOF

# Update desktop database
update-desktop-database ~/.local/share/applications/
```

Now you can search for “Viplyr” in your application menu and use it like any other installed program.

---

## 6. Controls

- `Space`: Play/Pause
- `R`: Rotate 90 degrees
- Double click video: Toggle fullscreen
- `Esc`: Exit fullscreen

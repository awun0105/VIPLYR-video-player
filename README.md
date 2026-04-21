# Viplyr - A Video Player on linux

Viplyr is an open-source media player written in **C++11** and **Qt5**. This project is optimized for Linux and currently support Linux environments (Ubuntu/Debian) only.

---

## 1. Folder Structure

The project strictly separates the Presentation Layer (UI) from the Core Logic Layer (Core):

```text
Viplyr/
├── build.sh                 # Automated cleanup & build script
├── CMakeLists.txt           # CMake project configuration
│
├── icon/
│   └── Viplyr_icon.png
│
└── src/
    ├── main.cpp             # Entry point, application initialization
    │
    ├── Utils/
    │   └── Theme.h          # Contains CSS/Styles, UI color hex codes
    │
    ├── Core/                # Core Logic Layer (No UI code)
    │   ├── MediaEngine.h
    │   └── MediaEngine.cpp  # Manages the Video decoder and Playlist
    │
    └── UI/                  # Presentation Layer (Handles display only)
        ├── Components.h/.cpp  # UI Lego blocks (Control bar, Buttons, StartScreen)
        ├── VideoCanvas.h/.cpp # Graphics rendering frame (Supports Zoom/Pan/Rotate)
        └── MainWindow.h/.cpp  # Central Controller
```

---

## 2. Architecture Overview

The project follows the **MVC (Model-View-Controller)** architecture combined with the **Facade Pattern**:
* **Decoupling:** The UI Layer is entirely oblivious to video decoding mechanics. Conversely, the Core Video Decoder has no knowledge of button colors or UI layouts.
* **Event-Driven Communication:** Components communicate exclusively via Qt's `Signals & Slots` mechanism, which is centrally routed in a single location to prevent spaghetti code.

---

## 3. Key Features

* **Drag & Drop:** Simply drag and drop media files directly into the UI to start playing.
* **Playback Controls:** Play, Pause, Stop, and Skip (+/- 5 seconds).
* **Smart Seeking:** Click directly on the timeline to jump to a specific time (no dragging required).
* **Real-time Counter:** Displays live playback duration (`00:00:00 / 01:23:45`).
* **Spatial Transforms:**
  * **Zoom & Pan:** Scroll the mouse wheel to zoom in/out, left-click and drag to pan the view.
  * **Rotate:** Click the `↺` / `↻` buttons (or use the `R` hotkey) to rotate the video 90 degrees (ideal for vertical smartphone videos).
* **Integrated Playlist:** Automatically generates a playlist when multiple files are dropped, supporting auto-play for the next track.

---

## 4. Detailed Components & Execution Flow

### Main Components:
1. **`MainWindow` (Controller Layer):**
   * Acts as the Conductor. It initializes all objects, arranges layouts, and contains the `wireConnections()` function to wire signals between the UI and the Core.
2. **`MediaEngine` (Core / Model Layer):**
   * Acts as a Facade that hides the complex underlying system APIs. It encapsulates the `QMediaPlayer` and `QMediaPlaylist` objects, exposing standard APIs like `play()`, `seekTo()`, and `setVolume()`.
3. **UI Components (View Layer):**
   * **`StartPanel`**: The standby welcome screen that handles *Drag & Drop* events.
   * **`VideoCanvas`**: Inherits `QGraphicsView`. It captures mouse/keyboard I/O to interpolate spatial transform matrices (Zoom/Pan/Rotate).
   * **`ControlBar`**: A container for interactive widgets (Play/Seek buttons, volume/time sliders, duration labels).

---

## 5. Build & Run

### Prerequisites
This project is optimized for Linux environments (Ubuntu/Debian). You need to install the following packages:
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
                 qtbase5-dev qtmultimedia5-dev libqt5multimediawidgets5
```

### Clone and Build
1. Clone the repository:
```bash
git clone [https://github.com/awun0105/VIPLYR-video-player.git](https://github.com/awun0105/VIPLYR-video-player.git)
cd VIPLYR-video-player
```

2. Grant execution permissions to the build script and run it:
```bash
chmod +x build.sh
./build.sh
```
*Note: The `build.sh` script automates CMake configuration (using Ninja for lightning-fast builds) and cleans up old cache files.*

### How to Launch
Once successfully built, there are two ways to run the application:

**Method 1: Via Terminal (Dev Environment)**
```bash
./build/Viplyr
```

**Method 2: As a Native Desktop Application (Linux)**
To integrate Viplyr into your Application Menu and enable "Open With..." functionality, run the following commands:
```bash
sudo cp build/Viplyr /usr/local/bin/Viplyr
cp <path/to/Viplyr_icon.png> ~/.local/share/icons/Viplyr_icon.png
nano ~/.local/share/applications/Viplyr.desktop
```
*(Refer to the internal documentation for the `.desktop` file configuration to link the icon and executable properly).* Then update the system database:

```bash
update-desktop-database ~/.local/share/applications/
```

You can now search for "Viplyr" in your system launcher and use it as a standalone application!

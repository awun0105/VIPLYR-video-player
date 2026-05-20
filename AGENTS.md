# Repository Guidelines

## Project Structure & Module Organization
`src/` contains all application code. `src/Core/` holds playback logic (`MediaEngine`), `src/UI/` contains Qt widgets and window orchestration, and `src/Utils/` centralizes UI styling (`Theme.h`), constants (`Constants.h`), and background logic like `SubtitleParser` and `ThumbnailExtractor`. `src/main.cpp` is the entry point. Static assets live in `icon/`. Build artifacts are generated under `build/` and should not be committed.

## Build, Test, and Development Commands
- `./setup.sh`: removes `build/`, configures CMake with Ninja, and compiles the app.
- `make setup`: same as `./setup.sh`.
- `make build`: incremental rebuild using Ninja (fast after small changes).
- `make run`: run the app (`./build/Viplyr`).
- `make` / `make start`: build then run.

Install prerequisites on Debian/Ubuntu with:

```bash
sudo apt install build-essential cmake ninja-build qtbase5-dev qtmultimedia5-dev libqt5multimediawidgets5
```

## Architecture & Logic
- **Strict MVC**: `MediaEngine` encapsulates all playback state. UI components should only interact with `MediaEngine` via its public API and signals. Avoid direct access to `QMediaPlayer` or `QMediaPlaylist`.
- **Asynchronous Tasks**: Heavy operations (filtering thousands of files, extracting thumbnails) must be done in background threads using `QtConcurrent` or dedicated extractor instances to keep the UI responsive.
- **Signal/Slot Mapping**: Centralize all cross-component wiring in `MainWindow::wireConnections()`.
- **Search & Filtering**: Subtitle lookups use binary search ($O(\log N)$) for efficiency. Playlist filtering is real-time and case-insensitive.

## Coding Style & Naming Conventions
Follow the existing C++11 and Qt style: 4-space indentation, opening braces on the same line, and header guards via `#pragma once`. Use `PascalCase` for classes and `camelCase` for methods. Prefer small, focused UI components (like `ThumbnailPopup` or `PlaylistPanel`) over adding complexity to `MainWindow`.

## Testing Guidelines
Verify changes with manual smoke tests:
1. Rebuild and launch.
2. Drag-and-drop large directories to test async loading.
3. Hover over the seek bar to verify thumbnail extraction.
4. Verify hotkeys: `Space` (Play/Pause), `F` (Fullscreen), `M` (Mute), `T` (Always on Top), `R` (Rotate), `Arrows` (Volume/Seek).
5. Load a video with a matching `.srt` file to verify subtitle sync.

## Commit & Pull Request Guidelines
Recent history mixes short messages (`Version 1`) with conventional prefixes (`chore:`). Prefer imperative, scoped subjects such as `feat: add playlist keyboard navigation` or `fix: clamp zoom level`. Keep commits focused.

Pull requests should describe the user-visible change, note any Linux/Qt dependencies, and include screenshots or short recordings for UI updates. Mention manual verification steps and any known limitations.

## Desktop Integration
The application supports standard Linux desktop integration. 
- **Icon**: Located at `icon/viplyr_icon.png`, should be copied to `~/.local/share/icons/`.
- **Desktop Entry**: `Viplyr.desktop` enables "Open with..." and default app association. Ensure the `Exec` line contains `%f` to receive file arguments.
- **MimeTypes**: Always update the `MimeType` field in the `.desktop` file when adding support for new formats in `Constants.h`.

## Architecture Notes
The project is intentionally split between UI and core playback logic. Preserve that separation: avoid putting media control logic into widgets when it belongs in `src/Core/`.

# Repository Guidelines

## Project Structure & Module Organization
`core/` is the playback engine `viplyr_core` (C++17 + FFmpeg, no Qt): public headers in `core/include/viplyr/`, implementation in `core/src/`, headless tests in `core/tests/`. `app/` is the Qt6 desktop player built on top of it: `PlayerController` (playback model), `VideoWidget` (OpenGL output), `AudioOutput`, `ThumbnailProvider`, `SubtitleTrack` and the widgets. `app/main.cpp` is the entry point. Design notes live in `docs/`. Static assets live in `icon/`. Build artifacts are generated under `build/` and should not be committed.

## Build, Test, and Development Commands
- `./setup.sh`: removes `build/`, configures CMake with Ninja, and compiles the app.
- `make setup`: same as `./setup.sh`.
- `make build`: incremental rebuild using Ninja (fast after small changes).
- `make run`: run the app (`./build/Viplyr`).
- `make` / `make start`: build then run.
- `make test`: run the headless tests (`ctest`): the engine, and real ffmpeg exports built by `ExportCommand`.

Install prerequisites on Debian/Ubuntu with:

```bash
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-multimedia-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libavfilter-dev libavdevice-dev libass-dev libegl-dev pybind11-dev python3-dev
```

## Architecture & Logic
- **One-way dependency**: `app/` depends on `core/`; `core/` must never include Qt or anything from `app/`.
- **Engine pipeline**: `Player` runs demux, video-decode, audio-decode and present threads connected by bounded queues. Every seek starts a new "serial"; packets and frames from an older serial are dropped.
- **Extension points**: new frame consumers implement `IVideoSink`; new audio outputs implement `IAudioSink`. Do not add interfaces without a second real implementation in sight.
- **UI rule**: widgets talk only to `PlayerController` (milliseconds, Qt signals). All wiring lives in `MainWindow::wireConnections()`.
- **Threads**: engine callbacks and `IVideoSink::present()` run on engine threads; hand over to the GUI thread before touching widgets. File scanning and thumbnails run on worker threads.

## Coding Style & Naming Conventions
C++17, 4-space indentation, opening braces on the same line, `#pragma once`. `PascalCase` for classes and files, `camelCase` for functions and variables, a trailing underscore for private members (`frameWidth_`), `kPascalCase` for constants. Prefer small, focused UI components (like `ThumbnailPopup` or `PlaylistPanel`) over adding complexity to `MainWindow`. Keep it simple: no abstraction without a concrete need.

## Testing Guidelines
Run `make test` after any change in `core/`. Then verify UI changes with manual smoke tests:
1. Rebuild and launch.
2. Drag-and-drop large directories to test async loading.
3. Hover over the seek bar to verify thumbnail extraction.
4. Verify hotkeys: `Space` (Play/Pause), `F` (Fullscreen), `M` (Mute), `T` (Always on Top), `R` (Rotate), `Arrows` (Volume/Seek), `,` / `.` (frame step), `I` (info), `S` (snapshot), `E` (export), `P` (playlist), `L` (A-B loop), `PgUp`/`PgDn` (chapters), `1`-`8`/`0` (picture controls).
5. Load a video with a matching `.srt` file to verify subtitle sync.

## Commit & Pull Request Guidelines
Recent history mixes short messages (`Version 1`) with conventional prefixes (`chore:`). Prefer imperative, scoped subjects such as `feat: add playlist keyboard navigation` or `fix: clamp zoom level`. Keep commits focused.

Pull requests should describe the user-visible change, note any Linux/Qt dependencies, and include screenshots or short recordings for UI updates. Mention manual verification steps and any known limitations.

## Desktop Integration
The application supports standard Linux desktop integration. 
- **Icon**: Located at `icon/viplyr_icon.png`, should be copied to `~/.local/share/icons/`.
- **Desktop Entry**: `Viplyr.desktop` enables "Open with..." and default app association. Ensure the `Exec` line contains `%f` to receive file arguments.
- **MimeTypes**: Always update the `MimeType` field in the `.desktop` file when adding support for new formats in `app/MediaFiles.cpp`.

## Architecture Notes
The project is intentionally split between UI and core playback logic. Preserve that separation: avoid putting media control logic into widgets when it belongs in `core/` or `PlayerController`.

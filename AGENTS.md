# Repository Guidelines

## Project Structure & Module Organization
`src/` contains all application code. `src/Core/` holds playback logic (`MediaEngine`), `src/UI/` contains Qt widgets and window orchestration, and `src/Utils/Theme.h` centralizes UI styling. `src/main.cpp` is the entry point. Static assets live in `icon/`. Build artifacts are generated under `build/` and should not be committed.

## Build, Test, and Development Commands
- `./setup.sh`: removes `build/`, configures CMake with Ninja, and compiles the app.
- `make setup`: same as `./setup.sh`.
- `make build`: incremental rebuild using Ninja (fast after small changes).
- `make run`: run the app (`./build/Viplyr`).
- `make` / `make start`: build then run.
- `cmake -S . -B build -G Ninja`: configure manually without running the full script.
- `cmake --build build`: rebuild from an existing configured `build/` directory.
- `./build/Viplyr`: run the desktop player locally after a successful build.

Install prerequisites on Debian/Ubuntu with:

```bash
sudo apt install build-essential cmake ninja-build qtbase5-dev qtmultimedia5-dev libqt5multimediawidgets5
```

## Coding Style & Naming Conventions
Follow the existing C++11 and Qt style in `src/`: 4-space indentation, opening braces on the same line, and header guards via `#pragma once`. Use `PascalCase` for classes (`MainWindow`, `VideoCanvas`), `camelCase` for methods (`loadAndPlay`, `wireConnections`), and keep Qt signal/slot wiring centralized in controller classes. Prefer small, focused UI components over large monolithic widgets.

## Testing Guidelines
There is currently no automated test suite in this repository. For changes, at minimum:
- rebuild with `make build` (or `./setup.sh` if you need a clean reconfigure)
- launch `./build/Viplyr`
- manually verify file loading, playback controls, seeking, and drag-and-drop behavior

When adding tests, keep them separate from production code and wire them through CMake rather than ad hoc scripts.

## Commit & Pull Request Guidelines
Recent history mixes short messages (`Version 1`) with conventional prefixes (`chore:`). Prefer imperative, scoped subjects such as `feat: add playlist keyboard navigation` or `fix: clamp zoom level`. Keep commits focused.

Pull requests should describe the user-visible change, note any Linux/Qt dependencies, and include screenshots or short recordings for UI updates. Mention manual verification steps and any known limitations.

## Architecture Notes
The project is intentionally split between UI and core playback logic. Preserve that separation: avoid putting media control logic into widgets when it belongs in `src/Core/`.

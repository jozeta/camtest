# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

camtest is a Windows "fake webcam": up to 4 virtual DirectShow camera devices, each streaming a
static image, animated GIF or folder slideshow (letterboxed, default 1920×1080) to any app that
picks a webcam (Teams, Zoom, browsers, OBS). Used to test camera-consuming apps. C++17, Win32,
CMake. **Windows only**: it cannot be built or run on macOS.

## Commands

From a Visual Studio 2022+ Developer PowerShell / Command Prompt (or after `vcvars64.bat`):

```
cmake -B build -G Ninja
cmake --build build
```

Produces `build/camtest.exe` and `build/camtest_filter.dll`, which must stay in the same folder.
Registering the devices needs admin (done from the GUI). `camtest.exe -autostart` starts serving
immediately. There is no automated test suite.

## Architecture

Two binaries that cannot be merged:

- **`camtest.exe`** (`src/main.cpp`, `src/Gui/`, `src/Engine/`, `src/AppConfig.*`,
  `src/VCamSlots.*`): the GUI. Configures slots, registers/unregisters the COM filter and runs a
  named-pipe server per active slot (`\\.\pipe\camtest_camN`) that pushes the current image path.
- **`camtest_filter.dll`** (`src/MediaSource/`): the DirectShow capture filter COM server,
  registered under `CLSID_VideoInputDeviceCategory`. It is loaded *into the consuming app's
  process* (Teams, browser, Frame Server), connects back to the exe's pipe as a client
  (auto-reconnecting), decodes and letterboxes frames and outputs RGB24.

Why not one exe: `InProcServer32` activation uses `LoadLibrary`, which needs a real DLL; loading
an exe that way re-runs its GUI inside the caller's process and crashes it.

Shared code: `src/Imaging/` (image/GIF loading, letterbox, RGB24 conversion), `src/Ipc/`
(pipe client/server and `ProtocolWireFormat.h`, the wire format both sides must agree on).
`spike/` holds early experiments; `third_party/baseclasses_unused/` is not built.

## Gotchas

- Code in the DLL runs inside other people's processes: never block, show UI or crash there;
  fail soft and output a placeholder frame instead.
- Consumers query formats/resolution before the pipe connects, so the DLL must answer from
  registry/config without the GUI running.
- Any change to the pipe protocol needs both sides updated together.

## App standards

This repo follows the app standards in `~/Desktop/code/tools/standards/STANDARDS.md` (rule IDs,
severities; decisions and waivers in `.claude-standards`). `claude-check --repo .` checks it.

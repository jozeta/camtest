# camtest

A minimal Windows "fake webcam": up to 4 virtual camera devices, each
broadcasting a single static image (letterboxed onto a 1920x1080 canvas) to
any app that can pick a webcam — Teams, Zoom, browsers, OBS, etc. Built for
testing camera-consuming apps without a real camera in front of you.

## How it works

The virtual cameras are implemented as a classic DirectShow capture filter,
registered under `CLSID_VideoInputDeviceCategory` so any DirectShow (and
most Media-Foundation-compatible) consumer can enumerate and select them.

The project ships as **two files that must stay together in the same
folder**:

- **`camtest.exe`** — the GUI you actually run. Lets you pick an image per
  camera slot, register/unregister the devices (one-time, needs
  admin), and Start/Stop serving frames. While running, it hosts a small
  named-pipe server per active slot (`\\.\pipe\camtest_camN`) that pushes
  the current image path to whatever has that camera open.
- **`camtest_filter.dll`** — the actual DirectShow COM server. This is what
  gets loaded (via `LoadLibrary`, triggered by `CoCreateInstance`) into
  *whatever process opens the camera* — Teams, a browser, the OS Frame
  Server, etc. It connects back to `camtest.exe`'s named pipe as a client,
  decodes/letterboxes/converts the selected image, and feeds it out as an
  RGB24 video stream.

These can't be merged into a single exe: `InProcServer32` COM activation
loads whatever it points at via `LoadLibrary`, which requires a real DLL. An
EXE's entry point is its full CRT + GUI startup — loading an EXE that way
re-enters the whole GUI application (dialog, message loop) inside the
*caller's* process and reliably crashes it.

## Building

Requires Visual Studio 2022+ (MSVC, C++17) and CMake. From a **Developer
Command Prompt / PowerShell** (or after running `vcvars64.bat`):

```powershell
cmake -B build -G Ninja
cmake --build build
```

This produces `build/camtest.exe` and `build/camtest_filter.dll`. Both are
statically linked against the CRT (no VC++ Redistributable needed), but they
must be distributed/run together from the same directory.

## Usage

1. Run `camtest.exe`.
2. Click **Register (admin, one-time)** — this registers up to 4 camera
   devices system-wide and requires an elevation prompt. It only needs to be
   done once (or again after moving the built files to a new location).
3. Pick how many cameras you want (1-4) and browse to an image for each.
4. Click **Start**.
5. Open Teams/Zoom/your browser/etc., and pick "camtest Virtual Camera N" as
   your webcam.
6. **Unregister** removes the devices again; closing camtest.exe (or Stop)
   just stops serving frames — already-registered devices stay listed
   (showing black) until something is picked for them again.

## Known limitations

- **Windows' built-in Camera app won't see these devices.** It only
  enumerates cameras through Media Foundation's frame-server pipeline (real
  hardware, or a proper `IMFVirtualCamera`-registered virtual camera).
  camtest registers as a classic DirectShow capture filter instead, which
  covers the overwhelming majority of real-world consumers (Teams, Zoom,
  OBS, browsers) but not that app specifically.
- Fixed at 1920x1080 @ 30fps, RGB24. No live camera feed — just a static
  image per slot (with hot-swap support while running).
- Windows only.

## Repo layout

```
src/                   Application source
  Gui/                 Win32 dialog (camtest.exe)
  Engine/               Registration + per-slot pipe-server orchestration
  MediaSource/          The DirectShow filter/pin/class factory (camtest_filter.dll)
  Imaging/              Image load / letterbox / RGB24 conversion
  Ipc/                  Named-pipe protocol between the two processes
third_party/baseclasses  Vendored Microsoft DirectShow BaseClasses (MIT)
spike/                  Standalone diagnostic tools used during development
                        (not part of the build; compile/run manually if needed)
```

## License

MIT — see [LICENSE](LICENSE). `third_party/baseclasses` is Microsoft's
DirectShow BaseClasses sample code, also MIT-licensed.

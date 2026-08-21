# camtest

A minimal Windows "fake webcam": up to 4 virtual camera devices, each
broadcasting a static image, an animated GIF, or a slideshow that cycles
through a folder (letterboxed onto a per-camera-configurable canvas, default
1920x1080) to any app that can pick a webcam — Teams, Zoom, browsers, OBS,
etc. Built for testing camera-consuming apps without a real camera in front
of you.

## How it works

The virtual cameras are implemented as a classic DirectShow capture filter,
registered under `CLSID_VideoInputDeviceCategory` so any DirectShow (and
most Media-Foundation-compatible) consumer can enumerate and select them.

The project ships as **two files that must stay together in the same
folder**:

- **`camtest.exe`** — the GUI you actually run. Lets you name each camera,
  pick an image (or a slideshow folder, or an animated GIF) per slot,
  configure resolution per slot, register/unregister the devices (one-time,
  needs admin), and Start/Stop serving frames. While running, it hosts a
  small named-pipe server per active slot (`\\.\pipe\camtest_camN`) that
  pushes the current image path to whatever has that camera open, and shows
  a live connected-client count and thumbnail per row.
- **`camtest_filter.dll`** — the actual DirectShow COM server. This is what
  gets loaded (via `LoadLibrary`, triggered by `CoCreateInstance`) into
  *whatever process opens the camera* — Teams, a browser, the OS Frame
  Server, etc. It connects back to `camtest.exe`'s named pipe as a client
  (reconnecting automatically if the GUI's server is stopped and restarted),
  decodes/letterboxes/converts the selected image or GIF frame sequence, and
  feeds it out as an RGB24 video stream at that slot's configured
  resolution.

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
   done once (or again after renaming a camera or changing its resolution —
   see below — or after moving the built files to a new location).
3. Pick how many cameras you want (1-4), give each a name, and browse to an
   image for each. Click a row's **Settings...** button to set its
   resolution or turn on slideshow mode (cycles through a folder of images
   on a timer instead of a single static one).
4. Click **Start**.
5. Open Teams/Zoom/your browser/etc., and pick the camera by the name you
   gave it (default "camtest Virtual Camera N").
6. **Unregister** removes the devices again; closing camtest.exe (or Stop)
   just stops serving frames — already-registered devices stay listed
   (showing black) until something is picked for them again. Stopping and
   restarting no longer requires restarting a consumer app that already has
   the camera open — it picks the new image back up automatically.

Renaming a camera or changing its resolution only takes effect for other
apps after clicking **Register** again — both are written into the
registry at registration time (same as the initial device registration),
since a consumer app queries them before camtest's own pipe connection can
possibly be up yet.

Pointing a slot at an animated `.gif` plays it back frame-by-frame (looping
indefinitely) instead of showing a static image — no separate toggle
needed, camtest just detects it has more than one frame.

Run `camtest.exe -autostart` to have it open straight into Start (using
whatever was configured last) — handy from a Windows startup shortcut.

## Known limitations

- **Windows' built-in Camera app won't see these devices.** It only
  enumerates cameras through Media Foundation's frame-server pipeline (real
  hardware, or a proper `IMFVirtualCamera`-registered virtual camera).
  camtest registers as a classic DirectShow capture filter instead, which
  covers the overwhelming majority of real-world consumers (Teams, Zoom,
  OBS, browsers) but not that app specifically.
- Always RGB24 @ 30fps; resolution is configurable per slot (a small preset
  list, default 1920x1080) but changing it needs a re-Register to take
  effect for other apps.
- No live camera feed — a static image, an animated GIF, or a slideshow
  cycling through a folder, per slot.
- Animated GIF playback composites frames using the common "do not dispose"
  convention (each frame's opaque pixels overwrite the previous ones at
  their declared offset). This covers the vast majority of real-world GIFs
  but doesn't implement the rarer "restore to background"/"restore to
  previous" disposal methods, so an unusual GIF relying on those may show
  minor ghosting. Frame count is capped at 500 to bound memory.
- Windows only.

## Repo layout

```
src/                   Application source
  Gui/                 Win32 dialogs: main window + per-camera Settings popup (camtest.exe)
  Engine/               Registration + per-slot pipe-server orchestration
  MediaSource/          The DirectShow filter/pin/class factory (camtest_filter.dll)
  Imaging/              Image/GIF load, compositing, letterbox, RGB24 conversion
  Ipc/                  Named-pipe protocol between the two processes
third_party/baseclasses  Vendored Microsoft DirectShow BaseClasses (MIT)
spike/                  Standalone diagnostic tools used during development
                        (not part of the build; compile/run manually if needed)
```

## License

MIT — see [LICENSE](LICENSE). `third_party/baseclasses` is Microsoft's
DirectShow BaseClasses sample code, also MIT-licensed.

#pragma once
#include <windows.h>
#include <string>
#include <array>

// Persists the last-used camera count and per-slot image paths to a small
// key=value text file placed next to the executable, so the app remains a
// portable, self-contained unit (exe + its own config file travel together).
struct AppConfig {
    static constexpr int kMaxCameras = 4;

    int cameraCount = 1;
    std::array<std::wstring, kMaxCameras> imagePaths;
    // Empty means "use the default (VCamSlots::FriendlyNameForSlot)". Only
    // takes effect for other applications after (re)registering, since the
    // name is what gets published as the DirectShow filter's friendly name.
    std::array<std::wstring, kMaxCameras> cameraNames;

    // 0 means "use the default (FrameBuffer::kDefaultWidth/Height)". Like
    // cameraNames, only takes effect for other applications after
    // (re)registering -- the pin reads this from the registry at
    // construction time, synchronously, before it can reach the GUI over
    // the pipe (see CamtestOutputPin.cpp).
    std::array<UINT32, kMaxCameras> frameWidth{};
    std::array<UINT32, kMaxCameras> frameHeight{};

    // Slideshow / cycling mode: when enabled for a slot, Start scans
    // slideshowFolders[i] for images and cycles through them every
    // slideshowIntervalSec[i] seconds instead of holding imagePaths[i].
    std::array<bool, kMaxCameras> slideshowEnabled{};
    std::array<std::wstring, kMaxCameras> slideshowFolders;
    std::array<int, kMaxCameras> slideshowIntervalSec{5, 5, 5, 5};

    bool registered = false;
    std::wstring registeredExePath;

    // Loads from <exeDir>\camtest.cfg. Missing file or missing keys fall back
    // to defaults (count=1, empty paths, not registered) rather than failing.
    static AppConfig Load();

    // Writes atomically (temp file + rename) so a mid-write crash can't
    // truncate the existing config.
    bool Save() const;
};

// Full path to camtest.cfg next to the running executable.
std::wstring GetConfigFilePath();

// Full path of the currently running executable.
std::wstring GetExecutablePath();

// Full path of camtest_filter.dll, expected to sit next to this executable
// -- that's the actual DirectShow in-proc COM server; camtest.exe itself is
// GUI-only and is never loaded in-proc by consumers.
std::wstring GetFilterDllPath();

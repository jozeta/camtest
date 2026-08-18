#pragma once
#include <string>
#include <array>

// Persists the last-used camera count and per-slot image paths to a small
// key=value text file placed next to the executable, so the app remains a
// portable, self-contained unit (exe + its own config file travel together).
struct AppConfig {
    static constexpr int kMaxCameras = 4;

    int cameraCount = 1;
    std::array<std::wstring, kMaxCameras> imagePaths;
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

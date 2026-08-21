#pragma once
#include <windows.h>
#include <array>
#include <vector>
#include <string>
#include "../Engine/VCamEngine.h"
#include "../AppConfig.h"

// The app's one and only window: a plain Win32 dialog. Owns the engine and
// the in-memory config, and is the only thing that ever touches the UI
// thread's message loop.
class MainDialog {
public:
    // Creates and shows the dialog, then runs the message loop until
    // closed. Returns the process exit code. If autoStart is set, Start is
    // triggered right after the window comes up (same validation/error
    // paths as clicking Start manually) -- for `-autostart`, e.g. from a
    // Windows startup shortcut.
    static int RunModal(HINSTANCE hInstance, bool autoStart = false);

private:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnInitDialog(HWND hDlg);
    void OnCountChanged();
    void OnBrowse(int slot);
    void OnNameChanged(int slot);
    bool SyncNameFromUi(int slot); // returns true if the name actually changed
    void OnSettings(int slot);
    void OnStart();
    void OnStop();
    void OnRegister();
    void OnUnregister();
    void RefreshRowVisibility();
    void RefreshStatus();
    void RefreshRowStatuses(); // per-row connected-client count, driven by kTickTimerId
    void RefreshPathLabel(int slot); // static image path, or "(slideshow) <folder>" when enabled
    void AdvanceSlideshows(); // driven by kTickTimerId; no-op per-slot unless running + slideshow enabled + due
    void UpdateSlideshowRowLabel(int slot); // "filename.png (3/12)"-style display while cycling
    void RefreshThumbnail(int slot, const std::wstring& imagePath); // static preview, first frame only
    void SaveConfig();

    static constexpr UINT_PTR kTickTimerId = 1;

    HINSTANCE hInstance_ = nullptr;
    HWND hDlg_ = nullptr;
    AppConfig config_;
    VCamEngine engine_;
    bool running_ = false;

    // Slideshow runtime state -- scanned fresh on each Start, not persisted.
    std::array<std::vector<std::wstring>, VCamSlots::kMaxSlots> slideshowFiles_;
    std::array<size_t, VCamSlots::kMaxSlots> slideshowIndex_{};
    std::array<ULONGLONG, VCamSlots::kMaxSlots> slideshowNextTick_{};

    // One GDI bitmap per row's thumbnail static control; owned here since
    // STM_SETIMAGE doesn't take ownership -- replaced (old one deleted) on
    // every RefreshThumbnail call, all deleted in WM_DESTROY.
    std::array<HBITMAP, VCamSlots::kMaxSlots> thumbnailBitmaps_{};
};

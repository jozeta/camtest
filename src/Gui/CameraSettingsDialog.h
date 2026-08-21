#pragma once
#include <windows.h>
#include "../AppConfig.h"

// Modal "Settings..." popup for a single camera slot -- resolution, plus
// slideshow folder/interval. Shown on demand from MainDialog (one dialog
// instance reused for whichever slot's Settings button was clicked, never
// a persistent per-slot window).
namespace CameraSettingsDialog {

struct Result {
    UINT32 frameWidth = 0;  // 0 = default (AppConfig's empty-means-default convention)
    UINT32 frameHeight = 0;
    bool slideshowEnabled = false;
    std::wstring slideshowFolder;
    int slideshowIntervalSec = 5;
};

// Shows the modal for `slot`, pre-filled from `config`. Returns true and
// fills `out` if the user clicked OK; returns false (out untouched) if
// cancelled.
bool Show(HWND owner, HINSTANCE hInstance, const AppConfig& config, int slot, Result& out);

} // namespace CameraSettingsDialog

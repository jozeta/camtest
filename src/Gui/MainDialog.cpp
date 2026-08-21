#include "MainDialog.h"
#include "resource.h"
#include "CameraSettingsDialog.h"
#include "../Imaging/ImageLoader.h"
#include "../Imaging/Letterbox.h"
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <array>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
constexpr int kThumbW = 50;
constexpr int kThumbH = 30;
}

namespace {

MainDialog* GetThis(HWND hDlg) {
    return reinterpret_cast<MainDialog*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
}

std::wstring TruncatedOrPlaceholder(const std::wstring& path) {
    return path.empty() ? L"(not set)" : path;
}

// Sorted list of *.png/*.jpg/*.jpeg/*.gif files directly inside `folder`
// (non-recursive). Empty on a missing/unreadable/empty folder.
std::vector<std::wstring> ScanImageFolder(const std::wstring& folder) {
    std::vector<std::wstring> files;
    if (folder.empty()) return files;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif") {
            files.push_back(entry.path().wstring());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Relaunches this exe elevated with the given argument, waits for it to
// finish, and returns its exit code (or -1 if it couldn't be launched /
// the user declined the UAC prompt).
int RunElevatedAndWait(const wchar_t* arg) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = arg;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        return -1;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    return (int)exitCode;
}

} // namespace

int MainDialog::RunModal(HINSTANCE hInstance, bool autoStart) {
    MainDialog dlg;
    dlg.hInstance_ = hInstance;
    HWND hDlg = CreateDialogParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DlgProc, (LPARAM)&dlg);
    if (!hDlg) return 1;

    ShowWindow(hDlg, SW_SHOW);
    if (autoStart) dlg.OnStart(); // reuses Start's own validation/error message boxes

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

INT_PTR CALLBACK MainDialog::DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* self = reinterpret_cast<MainDialog*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)self);
        self->hDlg_ = hDlg;
        self->OnInitDialog(hDlg);
        return TRUE;
    }
    case WM_COMMAND: {
        auto* self = GetThis(hDlg);
        if (!self) return FALSE;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == IDC_COUNT_COMBO && code == CBN_SELCHANGE) {
            self->OnCountChanged();
        } else if (id >= IDC_BROWSE_BASE && id < IDC_BROWSE_BASE + VCamSlots::kMaxSlots) {
            self->OnBrowse(id - IDC_BROWSE_BASE);
        } else if (id >= IDC_NAME_BASE && id < IDC_NAME_BASE + VCamSlots::kMaxSlots && code == EN_KILLFOCUS) {
            self->OnNameChanged(id - IDC_NAME_BASE);
        } else if (id >= IDC_SETTINGS_BASE && id < IDC_SETTINGS_BASE + VCamSlots::kMaxSlots) {
            self->OnSettings(id - IDC_SETTINGS_BASE);
        } else if (id == IDC_START) {
            self->OnStart();
        } else if (id == IDC_STOP) {
            self->OnStop();
        } else if (id == IDC_REGISTER) {
            self->OnRegister();
        } else if (id == IDC_UNREGISTER) {
            self->OnUnregister();
        }
        return TRUE;
    }
    case WM_TIMER: {
        auto* self = GetThis(hDlg);
        if (self && wParam == MainDialog::kTickTimerId) {
            self->RefreshRowStatuses();
            self->AdvanceSlideshows();
        }
        return TRUE;
    }
    case WM_CLOSE: {
        auto* self = GetThis(hDlg);
        if (self) {
            for (int i = 0; i < VCamSlots::kMaxSlots; ++i) self->SyncNameFromUi(i);
            self->SaveConfig();
        }
        DestroyWindow(hDlg);
        return TRUE;
    }
    case WM_DESTROY: {
        auto* self = GetThis(hDlg);
        if (self) {
            for (HBITMAP bmp : self->thumbnailBitmaps_) {
                if (bmp) DeleteObject(bmp);
            }
        }
        KillTimer(hDlg, MainDialog::kTickTimerId);
        PostQuitMessage(0);
        return TRUE;
    }
    }
    return FALSE;
}

void MainDialog::OnInitDialog(HWND hDlg) {
    config_ = AppConfig::Load();

    HWND combo = GetDlgItem(hDlg, IDC_COUNT_COMBO);
    for (int i = 1; i <= VCamSlots::kMaxSlots; ++i) {
        wchar_t buf[4];
        swprintf_s(buf, L"%d", i);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessageW(combo, CB_SETCURSEL, config_.cameraCount - 1, 0);

    for (int i = 0; i < VCamSlots::kMaxSlots; ++i) {
        RefreshPathLabel(i);
        std::wstring name = config_.cameraNames[i].empty() ? VCamSlots::FriendlyNameForSlot(i) : config_.cameraNames[i];
        SetDlgItemTextW(hDlg, IDC_NAME_BASE + i, name.c_str());
        // Slideshow slots don't have a representative image until Start
        // scans the folder, so they start blank.
        RefreshThumbnail(i, config_.slideshowEnabled[i] ? L"" : config_.imagePaths[i]);
    }

    RefreshRowVisibility();
    RefreshStatus();
    SetTimer(hDlg, kTickTimerId, 1000, nullptr);
}

void MainDialog::OnCountChanged() {
    if (running_) return; // combo is disabled while running; ignore defensively
    HWND combo = GetDlgItem(hDlg_, IDC_COUNT_COMBO);
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    config_.cameraCount = sel + 1;
    RefreshRowVisibility();
    SaveConfig();
}

void MainDialog::RefreshRowVisibility() {
    for (int i = 0; i < VCamSlots::kMaxSlots; ++i) {
        BOOL enable = (i < config_.cameraCount) ? TRUE : FALSE;
        EnableWindow(GetDlgItem(hDlg_, IDC_BROWSE_BASE + i), enable && !running_);
        EnableWindow(GetDlgItem(hDlg_, IDC_PATH_BASE + i), enable);
        EnableWindow(GetDlgItem(hDlg_, IDC_NAME_BASE + i), enable);
        EnableWindow(GetDlgItem(hDlg_, IDC_SETTINGS_BASE + i), enable && !running_);
    }
    EnableWindow(GetDlgItem(hDlg_, IDC_COUNT_COMBO), !running_);
}

void MainDialog::OnBrowse(int slot) {
    wchar_t fileBuf[MAX_PATH] = {};
    if (!config_.imagePaths[slot].empty()) {
        wcsncpy_s(fileBuf, config_.imagePaths[slot].c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hDlg_;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All files\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;

    config_.imagePaths[slot] = fileBuf;
    RefreshPathLabel(slot); // no-op display-wise if this slot's slideshow is enabled
    if (!config_.slideshowEnabled[slot]) RefreshThumbnail(slot, config_.imagePaths[slot]);
    SaveConfig();

    // Live swap if this slot is currently serving frames and isn't in
    // slideshow mode (which owns the image while it's cycling).
    if (running_ && slot < config_.cameraCount && !config_.slideshowEnabled[slot]) {
        engine_.SetImagePath(slot, config_.imagePaths[slot]);
    }
}

void MainDialog::RefreshPathLabel(int slot) {
    std::wstring text = config_.slideshowEnabled[slot]
        ? L"(slideshow) " + TruncatedOrPlaceholder(config_.slideshowFolders[slot])
        : TruncatedOrPlaceholder(config_.imagePaths[slot]);
    SetDlgItemTextW(hDlg_, IDC_PATH_BASE + slot, text.c_str());
}

void MainDialog::UpdateSlideshowRowLabel(int slot) {
    const auto& files = slideshowFiles_[slot];
    if (files.empty() || slideshowIndex_[slot] >= files.size()) return;
    std::wstring filename = fs::path(files[slideshowIndex_[slot]]).filename().wstring();
    std::wstring text = filename + L" (" + std::to_wstring(slideshowIndex_[slot] + 1) +
                         L"/" + std::to_wstring(files.size()) + L")";
    SetDlgItemTextW(hDlg_, IDC_PATH_BASE + slot, text.c_str());
}

void MainDialog::RefreshThumbnail(int slot, const std::wstring& imagePath) {
    std::vector<uint8_t> canvas;
    bool ok = false;
    if (!imagePath.empty()) {
        std::vector<uint8_t> pixels;
        UINT w = 0, h = 0;
        if (ImageLoader::LoadBGRA32(imagePath, pixels, w, h)) { // first frame only -- a static preview
            ok = Letterbox::ComposeCanvas(pixels, w, h, kThumbW, kThumbH, canvas);
        }
    }

    HBITMAP newBitmap = nullptr;
    if (ok) {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = kThumbW;
        bmi.bmiHeader.biHeight = -kThumbH; // negative = top-down, matches ComposeCanvas's layout
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        newBitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (newBitmap && bits) {
            memcpy(bits, canvas.data(), canvas.size());
        } else if (newBitmap) {
            DeleteObject(newBitmap);
            newBitmap = nullptr;
        }
    }

    SendDlgItemMessageW(hDlg_, IDC_THUMB_BASE + slot, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)newBitmap);
    if (thumbnailBitmaps_[slot]) DeleteObject(thumbnailBitmaps_[slot]); // STM_SETIMAGE doesn't take ownership
    thumbnailBitmaps_[slot] = newBitmap;
}

void MainDialog::AdvanceSlideshows() {
    if (!running_) return;
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < config_.cameraCount; ++i) {
        if (!config_.slideshowEnabled[i] || slideshowFiles_[i].empty()) continue;
        if (now < slideshowNextTick_[i]) continue;

        slideshowIndex_[i] = (slideshowIndex_[i] + 1) % slideshowFiles_[i].size();
        slideshowNextTick_[i] = now + (ULONGLONG)config_.slideshowIntervalSec[i] * 1000;
        engine_.SetImagePath(i, slideshowFiles_[i][slideshowIndex_[i]]);
        UpdateSlideshowRowLabel(i);
        RefreshThumbnail(i, slideshowFiles_[i][slideshowIndex_[i]]);
    }
}

bool MainDialog::SyncNameFromUi(int slot) {
    wchar_t buf[128];
    GetDlgItemTextW(hDlg_, IDC_NAME_BASE + slot, buf, 128);
    std::wstring typed = buf;

    std::wstring defaultName = VCamSlots::FriendlyNameForSlot(slot);
    std::wstring name = typed.empty() ? defaultName : typed;
    if (name != typed) {
        SetDlgItemTextW(hDlg_, IDC_NAME_BASE + slot, name.c_str()); // don't allow a blank name
    }

    std::wstring previous = config_.cameraNames[slot].empty() ? defaultName : config_.cameraNames[slot];
    if (name == previous) return false; // no actual change -- e.g. just tabbed through

    config_.cameraNames[slot] = (name == defaultName) ? L"" : name;
    return true;
}

void MainDialog::OnNameChanged(int slot) {
    if (!SyncNameFromUi(slot)) return;
    SaveConfig();

    if (config_.registered) {
        MessageBoxW(hDlg_, L"Camera name saved. Click \"Register\" again to update the name shown in other apps.",
                    L"camtest", MB_OK | MB_ICONINFORMATION);
    }
}

void MainDialog::OnSettings(int slot) {
    CameraSettingsDialog::Result result;
    if (!CameraSettingsDialog::Show(hDlg_, hInstance_, config_, slot, result)) return; // cancelled

    bool resolutionChanged = (config_.frameWidth[slot] != result.frameWidth) || (config_.frameHeight[slot] != result.frameHeight);
    bool slideshowChanged = (config_.slideshowEnabled[slot] != result.slideshowEnabled) ||
                             (config_.slideshowFolders[slot] != result.slideshowFolder) ||
                             (config_.slideshowIntervalSec[slot] != result.slideshowIntervalSec);
    if (!resolutionChanged && !slideshowChanged) return;

    config_.frameWidth[slot] = result.frameWidth;
    config_.frameHeight[slot] = result.frameHeight;
    config_.slideshowEnabled[slot] = result.slideshowEnabled;
    config_.slideshowFolders[slot] = result.slideshowFolder;
    config_.slideshowIntervalSec[slot] = result.slideshowIntervalSec;
    SaveConfig();

    if (!running_) {
        RefreshPathLabel(slot); // reflect a slideshow on/off toggle immediately; while
                                 // running the row already shows live cycling state
        RefreshThumbnail(slot, config_.slideshowEnabled[slot] ? L"" : config_.imagePaths[slot]);
    }

    if (resolutionChanged && config_.registered) {
        MessageBoxW(hDlg_, L"Resolution saved. Click \"Register\" again to update it for other apps.",
                    L"camtest", MB_OK | MB_ICONINFORMATION);
    }
}

void MainDialog::OnStart() {
    if (running_) return;

    for (int i = 0; i < config_.cameraCount; ++i) {
        if (config_.slideshowEnabled[i]) {
            slideshowFiles_[i] = ScanImageFolder(config_.slideshowFolders[i]);
            if (slideshowFiles_[i].empty()) {
                MessageBoxW(hDlg_, (L"Slideshow folder for camera " + std::to_wstring(i + 1) +
                                     L" has no images (.png/.jpg/.jpeg/.gif).").c_str(),
                            L"camtest", MB_OK | MB_ICONWARNING);
                return;
            }
        } else if (config_.imagePaths[i].empty()) {
            MessageBoxW(hDlg_, L"Pick an image for every active camera slot before starting.",
                        L"camtest", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    int startedCount = 0;
    for (int i = 0; i < config_.cameraCount; ++i) {
        bool slideshow = config_.slideshowEnabled[i];
        std::wstring initialPath = slideshow ? slideshowFiles_[i][0] : config_.imagePaths[i];
        if (engine_.StartSlot(i, initialPath)) {
            ++startedCount;
            if (slideshow) {
                slideshowIndex_[i] = 0;
                slideshowNextTick_[i] = GetTickCount64() + (ULONGLONG)config_.slideshowIntervalSec[i] * 1000;
                UpdateSlideshowRowLabel(i);
                RefreshThumbnail(i, slideshowFiles_[i][0]);
            }
        }
    }

    if (startedCount == 0) {
        MessageBoxW(hDlg_, L"Couldn't start any camera. Have you registered the app yet?",
                    L"camtest", MB_OK | MB_ICONERROR);
        return;
    }
    if (startedCount < config_.cameraCount) {
        MessageBoxW(hDlg_, L"Some cameras failed to start; the rest are running.",
                    L"camtest", MB_OK | MB_ICONWARNING);
    }

    running_ = true;
    RefreshRowVisibility();
    RefreshStatus();
}

void MainDialog::OnStop() {
    if (!running_) return;
    for (int i = 0; i < VCamSlots::kMaxSlots; ++i) {
        engine_.StopSlot(i);
        RefreshPathLabel(i); // drop the "(3/12)" slideshow-cycling display, if any
    }
    running_ = false;
    RefreshRowVisibility();
    RefreshStatus();
}

void MainDialog::OnRegister() {
    int result = RunElevatedAndWait(L"-RegServer");
    if (result != 0) {
        MessageBoxW(hDlg_, L"Registration was cancelled or failed.", L"camtest", MB_OK | MB_ICONWARNING);
    }
    config_ = AppConfig::Load(); // VCamEngine::RegisterAllSlots persisted the result
    RefreshStatus();
}

void MainDialog::OnUnregister() {
    int result = RunElevatedAndWait(L"-UnregServer");
    if (result != 0) {
        MessageBoxW(hDlg_, L"Unregistration was cancelled or failed.", L"camtest", MB_OK | MB_ICONWARNING);
    }
    config_ = AppConfig::Load();
    RefreshStatus();
}

void MainDialog::RefreshStatus() {
    std::wstring status;
    if (!config_.registered) {
        status = L"Not registered yet -- click Register before Start will work.";
    } else if (running_) {
        status = L"Registered. " + std::to_wstring(config_.cameraCount) + L" camera(s) running.";
    } else {
        status = L"Registered. Not currently running.";
    }
    SetDlgItemTextW(hDlg_, IDC_STATUS, status.c_str());
}

void MainDialog::RefreshRowStatuses() {
    for (int i = 0; i < VCamSlots::kMaxSlots; ++i) {
        std::wstring text;
        if (running_ && i < config_.cameraCount) {
            int count = engine_.GetConnectedClientCount(i);
            if (count > 0) {
                text = L"●" + std::to_wstring(count); // "●N"
            }
        }
        SetDlgItemTextW(hDlg_, IDC_ROWSTATUS_BASE + i, text.c_str());
    }
}

void MainDialog::SaveConfig() {
    config_.Save();
}

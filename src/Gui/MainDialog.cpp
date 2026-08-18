#include "MainDialog.h"
#include "resource.h"
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <array>

namespace {

MainDialog* GetThis(HWND hDlg) {
    return reinterpret_cast<MainDialog*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
}

std::wstring TruncatedOrPlaceholder(const std::wstring& path) {
    return path.empty() ? L"(not set)" : path;
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

int MainDialog::RunModal(HINSTANCE hInstance) {
    MainDialog dlg;
    HWND hDlg = CreateDialogParamW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DlgProc, (LPARAM)&dlg);
    if (!hDlg) return 1;

    ShowWindow(hDlg, SW_SHOW);

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
    case WM_CLOSE: {
        auto* self = GetThis(hDlg);
        if (self) self->SaveConfig();
        DestroyWindow(hDlg);
        return TRUE;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
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
        SetDlgItemTextW(hDlg, IDC_PATH_BASE + i, TruncatedOrPlaceholder(config_.imagePaths[i]).c_str());
    }

    RefreshRowVisibility();
    RefreshStatus();
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
    SetDlgItemTextW(hDlg_, IDC_PATH_BASE + slot, fileBuf);
    SaveConfig();

    // Live swap if this slot is currently serving frames.
    if (running_ && slot < config_.cameraCount) {
        engine_.SetImagePath(slot, config_.imagePaths[slot]);
    }
}

void MainDialog::OnStart() {
    if (running_) return;

    for (int i = 0; i < config_.cameraCount; ++i) {
        if (config_.imagePaths[i].empty()) {
            MessageBoxW(hDlg_, L"Pick an image for every active camera slot before starting.",
                        L"camtest", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    int startedCount = 0;
    for (int i = 0; i < config_.cameraCount; ++i) {
        if (engine_.StartSlot(i, config_.imagePaths[i])) {
            ++startedCount;
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

void MainDialog::SaveConfig() {
    config_.Save();
}

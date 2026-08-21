#include "CameraSettingsDialog.h"
#include "resource.h"
#include "../Imaging/FrameBuffer.h"
#include <shobjidl.h>
#include <string>

namespace {

struct ResPreset {
    UINT32 w, h;
    const wchar_t* label;
};
constexpr ResPreset kPresets[] = {
    {1920, 1080, L"1920x1080"},
    {1280, 720, L"1280x720"},
    {640, 480, L"640x480"},
    {320, 240, L"320x240"},
};
constexpr int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

struct DlgContext {
    const AppConfig* config;
    int slot;
    CameraSettingsDialog::Result result;
    bool accepted = false;
};

DlgContext* GetContext(HWND hDlg) {
    return reinterpret_cast<DlgContext*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
}

void RefreshSlideshowControlsEnabled(HWND hDlg) {
    BOOL enabled = IsDlgButtonChecked(hDlg, IDC_SET_SLIDESHOW_ENABLE) == BST_CHECKED;
    EnableWindow(GetDlgItem(hDlg, IDC_SET_SLIDESHOW_FOLDER), enabled);
    EnableWindow(GetDlgItem(hDlg, IDC_SET_SLIDESHOW_BROWSE), enabled);
    EnableWindow(GetDlgItem(hDlg, IDC_SET_SLIDESHOW_INTERVAL), enabled);
}

// Modern folder picker via IFileOpenDialog (FOS_PICKFOLDERS). Returns false
// if the user cancelled or it failed for any reason.
bool BrowseForFolder(HWND owner, std::wstring& outFolder) {
    bool ok = false;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD opts = 0;
        if (SUCCEEDED(dialog->GetOptions(&opts))) {
            dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        }
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    outFolder = path;
                    CoTaskMemFree(path);
                    ok = true;
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    return ok;
}

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* ctx = reinterpret_cast<DlgContext*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);

        UINT32 curW = ctx->config->frameWidth[ctx->slot] != 0 ? ctx->config->frameWidth[ctx->slot] : FrameBuffer::kDefaultWidth;
        UINT32 curH = ctx->config->frameHeight[ctx->slot] != 0 ? ctx->config->frameHeight[ctx->slot] : FrameBuffer::kDefaultHeight;

        HWND combo = GetDlgItem(hDlg, IDC_SET_RES_COMBO);
        int selectIndex = 0;
        for (int i = 0; i < kPresetCount; ++i) {
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)kPresets[i].label);
            if (kPresets[i].w == curW && kPresets[i].h == curH) selectIndex = i;
        }
        SendMessageW(combo, CB_SETCURSEL, selectIndex, 0);

        CheckDlgButton(hDlg, IDC_SET_SLIDESHOW_ENABLE,
                       ctx->config->slideshowEnabled[ctx->slot] ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(hDlg, IDC_SET_SLIDESHOW_FOLDER, ctx->config->slideshowFolders[ctx->slot].c_str());
        SetDlgItemInt(hDlg, IDC_SET_SLIDESHOW_INTERVAL, (UINT)ctx->config->slideshowIntervalSec[ctx->slot], FALSE);
        RefreshSlideshowControlsEnabled(hDlg);
        return TRUE;
    }
    case WM_COMMAND: {
        auto* ctx = GetContext(hDlg);
        if (!ctx) return FALSE;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == IDC_SET_SLIDESHOW_ENABLE && code == BN_CLICKED) {
            RefreshSlideshowControlsEnabled(hDlg);
            return TRUE;
        }
        if (id == IDC_SET_SLIDESHOW_BROWSE) {
            std::wstring folder;
            if (BrowseForFolder(hDlg, folder)) {
                SetDlgItemTextW(hDlg, IDC_SET_SLIDESHOW_FOLDER, folder.c_str());
            }
            return TRUE;
        }
        if (id == IDOK) {
            int sel = (int)SendDlgItemMessageW(hDlg, IDC_SET_RES_COMBO, CB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= kPresetCount) sel = 0;
            ctx->result.frameWidth = kPresets[sel].w;
            ctx->result.frameHeight = kPresets[sel].h;

            ctx->result.slideshowEnabled = IsDlgButtonChecked(hDlg, IDC_SET_SLIDESHOW_ENABLE) == BST_CHECKED;
            wchar_t folderBuf[MAX_PATH] = {};
            GetDlgItemTextW(hDlg, IDC_SET_SLIDESHOW_FOLDER, folderBuf, MAX_PATH);
            ctx->result.slideshowFolder = folderBuf;
            BOOL translated = FALSE;
            UINT interval = GetDlgItemInt(hDlg, IDC_SET_SLIDESHOW_INTERVAL, &translated, FALSE);
            ctx->result.slideshowIntervalSec = (translated && interval >= 1) ? (int)interval : 5;

            if (ctx->result.slideshowEnabled && ctx->result.slideshowFolder.empty()) {
                MessageBoxW(hDlg, L"Pick a folder to enable slideshow mode.", L"camtest", MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            ctx->accepted = true;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

} // namespace

namespace CameraSettingsDialog {

bool Show(HWND owner, HINSTANCE hInstance, const AppConfig& config, int slot, Result& out) {
    DlgContext ctx;
    ctx.config = &config;
    ctx.slot = slot;
    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_CAMSETTINGS), owner, DlgProc, (LPARAM)&ctx);
    if (!ctx.accepted) return false;
    out = ctx.result;
    return true;
}

} // namespace CameraSettingsDialog

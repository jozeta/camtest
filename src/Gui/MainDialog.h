#pragma once
#include <windows.h>
#include "../Engine/VCamEngine.h"
#include "../AppConfig.h"

// The app's one and only window: a plain Win32 dialog. Owns the engine and
// the in-memory config, and is the only thing that ever touches the UI
// thread's message loop.
class MainDialog {
public:
    // Creates and shows the dialog, then runs the message loop until
    // closed. Returns the process exit code.
    static int RunModal(HINSTANCE hInstance);

private:
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnInitDialog(HWND hDlg);
    void OnCountChanged();
    void OnBrowse(int slot);
    void OnStart();
    void OnStop();
    void OnRegister();
    void OnUnregister();
    void RefreshRowVisibility();
    void RefreshStatus();
    void SaveConfig();

    HWND hDlg_ = nullptr;
    AppConfig config_;
    VCamEngine engine_;
    bool running_ = false;
};

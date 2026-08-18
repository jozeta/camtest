#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include <shellapi.h>
#include "Gui/MainDialog.h"
#include "Engine/VCamEngine.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring mode = (argc >= 2) ? argv[1] : L"";
    if (argv) LocalFree(argv);

    int exitCode = 0;
    if (mode == L"-RegServer") {
        std::wstring err;
        exitCode = VCamEngine::RegisterAllSlots(err) ? 0 : 1;
    } else if (mode == L"-UnregServer") {
        std::wstring err;
        exitCode = VCamEngine::UnregisterAllSlots(err) ? 0 : 1;
    } else {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);
        exitCode = MainDialog::RunModal(hInstance);
    }

    CoUninitialize();
    return exitCode;
}

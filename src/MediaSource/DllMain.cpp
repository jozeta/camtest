#include <windows.h>

// Real, minimal DLL entry point. This module is loaded via LoadLibrary by
// whatever process CoCreateInstance's one of our filter CLSIDs (Teams, a
// browser, the OS Frame Server, ...) -- nothing here should do real work,
// just satisfy the loader. (camtest.exe used to try to double as this COM
// server itself; LoadLibrary-ing an EXE re-runs its WinMainCRTStartup entry
// point under DLL_PROCESS_ATTACH semantics, which re-entered the full GUI
// app -- dialog, message loop -- inside the caller's process and blew the
// stack. Splitting the filter into a real DLL with a real DllMain avoids
// that entirely.)
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}

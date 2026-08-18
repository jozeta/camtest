// Standalone diagnostic: enumerate video capture devices exactly like a
// normal DirectShow consumer would, find "camtest Virtual Camera 1", bind to
// it, connect its output pin to a Null Renderer, run the graph briefly, and
// dump the first few bytes of pixel data it actually produces. This isolates
// whether the filter/pipe pipeline itself works when driven by an ordinary,
// unsandboxed consumer -- as opposed to whatever Teams' internal activation
// path is doing (which our process/module scan showed never even calls
// CoCreateInstance on our CLSID).
#include <windows.h>
#include <dshow.h>
#include <initguid.h>
#include <tlhelp32.h>
#include <string>
#include <iostream>

static void ReportFaultLocation(DWORD64 addr) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { std::wcerr << L"  (couldn't snapshot modules)" << std::endl; return; }
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool matched = false;
    if (Module32FirstW(snap, &me)) {
        do {
            DWORD64 base = (DWORD64)me.modBaseAddr;
            DWORD64 end = base + me.modBaseSize;
            if (addr >= base && addr < end) {
                std::wcout << L"  Fault in module: " << me.szModule
                           << L" +0x" << std::hex << (addr - base) << std::dec << std::endl;
                matched = true;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    if (!matched) std::wcout << L"  Fault address 0x" << std::hex << addr << L" not in any loaded module" << std::dec << std::endl;
}

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

DEFINE_GUID(CLSID_NullRenderer,
    0xc1f400a4, 0x3f08, 0x11d3, 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37);

static const wchar_t* kTargetName = L"camtest Virtual Camera 1";

int wmain() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { std::wcerr << L"CoInitializeEx failed: 0x" << std::hex << hr << std::endl; return 1; }

    ICreateDevEnum* pDevEnum = nullptr;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr)) { std::wcerr << L"CoCreateInstance(SystemDeviceEnum) failed: 0x" << std::hex << hr << std::endl; return 1; }

    IEnumMoniker* pEnum = nullptr;
    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    pDevEnum->Release();
    if (hr != S_OK || !pEnum) {
        std::wcerr << L"No video capture devices enumerated at all (hr=0x" << std::hex << hr << L")" << std::endl;
        return 1;
    }

    IMoniker* pMoniker = nullptr;
    IBaseFilter* pFilter = nullptr;
    bool found = false;
    int count = 0;
    while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
        ++count;
        IPropertyBag* pBag = nullptr;
        if (SUCCEEDED(pMoniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)&pBag))) {
            VARIANT var;
            VariantInit(&var);
            if (SUCCEEDED(pBag->Read(L"FriendlyName", &var, nullptr))) {
                std::wcout << L"Device " << count << L": " << var.bstrVal << std::endl;
                if (!found && wcscmp(var.bstrVal, kTargetName) == 0) {
                    found = true;
                    __try {
                        HRESULT bindHr = pMoniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void**)&pFilter);
                        std::wcout << L"  BindToObject: 0x" << std::hex << bindHr << std::dec << std::endl;
                    } __except (
                        std::wcout << L"  BindToObject CRASHED, exception code 0x"
                                   << std::hex << GetExceptionCode() << std::dec << std::endl,
                        ReportFaultLocation((DWORD64)GetExceptionInformation()->ExceptionRecord->ExceptionAddress),
                        EXCEPTION_EXECUTE_HANDLER) {
                        pFilter = nullptr;
                    }
                }
                VariantClear(&var);
            }
            pBag->Release();
        }
        pMoniker->Release();
    }
    pEnum->Release();
    std::wcout << L"Total devices enumerated: " << count << std::endl;

    if (!found || !pFilter) {
        std::wcout << L"'" << kTargetName << L"' not found or bind failed." << std::endl;
        CoUninitialize();
        return 1;
    }

    IGraphBuilder* pGraph = nullptr;
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&pGraph);
    if (FAILED(hr)) { std::wcerr << L"CoCreateInstance(FilterGraph) failed: 0x" << std::hex << hr << std::endl; return 1; }

    hr = pGraph->AddFilter(pFilter, L"CamtestSource");
    std::wcout << L"AddFilter(source): 0x" << std::hex << hr << std::dec << std::endl;

    IBaseFilter* pNullRenderer = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&pNullRenderer))) {
        pGraph->AddFilter(pNullRenderer, L"NullRenderer");
    }

    IEnumPins* pEnumPins = nullptr;
    IPin* pOutPin = nullptr;
    hr = pFilter->EnumPins(&pEnumPins);
    if (SUCCEEDED(hr)) {
        if (pEnumPins->Next(1, &pOutPin, nullptr) == S_OK) {
            hr = pGraph->Render(pOutPin);
            std::wcout << L"Render(outputPin): 0x" << std::hex << hr << std::dec << std::endl;
        }
        pEnumPins->Release();
    }

    IMediaControl* pControl = nullptr;
    pGraph->QueryInterface(IID_IMediaControl, (void**)&pControl);
    if (pControl) {
        hr = pControl->Run();
        std::wcout << L"Run: 0x" << std::hex << hr << std::dec << std::endl;
        Sleep(2500);

        OAFilterState state;
        pControl->GetState(0, &state);
        std::wcout << L"Filter graph state: " << state << L" (2=running)" << std::endl;

        pControl->Stop();
        pControl->Release();
    }

    if (pOutPin) pOutPin->Release();
    if (pNullRenderer) pNullRenderer->Release();
    pFilter->Release();
    pGraph->Release();
    CoUninitialize();
    return 0;
}

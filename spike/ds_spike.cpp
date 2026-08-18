// DirectShow spike: minimal push-source capture filter, self-hosted in this
// exe (reusing the validated DllGetClassObject/DllCanUnloadNow trick),
// registered as a Video Capture Source, verified programmatically via
// ICreateDevEnum + a minimal graph (no GUI/consumer app needed).
//
// Modes:
//   -register     register CLSID + filter category (needs admin)
//   -unregister   remove both (needs admin)
//   -enumtest     enumerate video capture devices, confirm our filter is
//                 listed, bind to it, build a graph with a null renderer,
//                 run it briefly, and count how many frames FillBuffer
//                 actually delivered (written to a log file since FillBuffer
//                 runs inside the SAME process for -enumtest, but inside
//                 whatever consumer loads us in the general case).

#include <windows.h>
#include <streams.h>
#include <initguid.h>
#include <string>
#include <fstream>
#include <iostream>
#include <atomic>

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")

// {B2C3D4E5-F6A7-4B5C-9D0E-1A2B3C4D5E7F} -- private test CLSID for the filter.
DEFINE_GUID(CLSID_SpikePushFilter,
    0xb2c3d4e5, 0xf6a7, 0x4b5c, 0x9d, 0x0e, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x7f);

// Dropped from the modern Windows SDK's strmiids; this is the well-known
// value from the classic DirectX SDK.
DEFINE_GUID(CLSID_NullRenderer,
    0xc1f400a4, 0x3f08, 0x11d3, 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37);

static const wchar_t* kFriendlyName = L"Spike DirectShow Camera";

static std::wstring ExeDir() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring p = exePath;
    size_t slash = p.find_last_of(L"\\/");
    return p.substr(0, slash);
}

static void Log(const std::wstring& msg) {
    std::wofstream f(ExeDir() + L"\\ds_spike_log.txt", std::ios::app);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    f << buf << L"PID=" << GetCurrentProcessId() << L" " << msg << std::endl;
}

static std::atomic<int> g_fillBufferCalls{0};

// ==================== push pin ====================
class SpikePushPin : public CSourceStream {
public:
    SpikePushPin(HRESULT* phr, CSource* pFilter)
        : CSourceStream(L"SpikePushPin", phr, pFilter, L"Out") {}

    HRESULT FillBuffer(IMediaSample* pSample) override {
        int n = ++g_fillBufferCalls;
        if (n <= 3 || n % 30 == 0) {
            Log(L"FillBuffer called, count=" + std::to_wstring(n));
        }

        BYTE* pData = nullptr;
        pSample->GetPointer(&pData);
        long size = pSample->GetSize();
        // Flat mid-gray RGB24 test pattern (bottom-up order doesn't matter
        // for a flat fill).
        memset(pData, 128, size);
        pSample->SetActualDataLength(size);

        REFERENCE_TIME start = m_frameIndex * kFrameDuration;
        REFERENCE_TIME end = start + kFrameDuration;
        pSample->SetTime(&start, &end);
        pSample->SetSyncPoint(TRUE);
        ++m_frameIndex;

        // Pace to ~30fps; simplest correct approach for a fixed-rate push source.
        Sleep(33);
        return S_OK;
    }

    HRESULT GetMediaType(CMediaType* pmt) override {
        VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
        if (!vih) return E_OUTOFMEMORY;
        ZeroMemory(vih, sizeof(VIDEOINFOHEADER));
        vih->AvgTimePerFrame = kFrameDuration;
        vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        vih->bmiHeader.biWidth = kWidth;
        vih->bmiHeader.biHeight = kHeight;
        vih->bmiHeader.biPlanes = 1;
        vih->bmiHeader.biBitCount = 24;
        vih->bmiHeader.biCompression = BI_RGB;
        vih->bmiHeader.biSizeImage = kWidth * kHeight * 3;

        pmt->SetType(&MEDIATYPE_Video);
        pmt->SetSubtype(&MEDIASUBTYPE_RGB24);
        pmt->SetFormatType(&FORMAT_VideoInfo);
        pmt->SetTemporalCompression(FALSE);
        pmt->SetSampleSize(vih->bmiHeader.biSizeImage);
        return S_OK;
    }

    HRESULT CheckMediaType(const CMediaType* pmt) override {
        if (*pmt->Type() != MEDIATYPE_Video) return S_FALSE;
        if (*pmt->Subtype() != MEDIASUBTYPE_RGB24) return S_FALSE;
        return S_OK;
    }

    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) override {
        pProps->cBuffers = 2;
        pProps->cbBuffer = kWidth * kHeight * 3;
        ALLOCATOR_PROPERTIES actual;
        HRESULT hr = pAlloc->SetProperties(pProps, &actual);
        if (FAILED(hr)) return hr;
        if (actual.cbBuffer < pProps->cbBuffer) return E_FAIL;
        return S_OK;
    }

private:
    static constexpr int kWidth = 1920;
    static constexpr int kHeight = 1080;
    static constexpr REFERENCE_TIME kFrameDuration = 333333; // 100ns units, ~30fps
    LONGLONG m_frameIndex = 0;
};

// ==================== filter ====================
class SpikePushFilter : public CSource {
public:
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
        auto* filter = new SpikePushFilter(pUnk, phr);
        if (phr && FAILED(*phr)) { delete filter; return nullptr; }
        return filter;
    }

    SpikePushFilter(LPUNKNOWN pUnk, HRESULT* phr)
        : CSource(L"SpikePushFilter", pUnk, CLSID_SpikePushFilter) {
        Log(L"SpikePushFilter constructed");
        m_pPin = new SpikePushPin(phr, this);
        if (phr) *phr = m_pPin ? S_OK : E_OUTOFMEMORY;
    }

private:
    SpikePushPin* m_pPin = nullptr; // owned via CSource's pin list (AddPin in CSourceStream ctor)
};

// ==================== minimal IClassFactory ====================
class SpikeClassFactory : public IClassFactory {
public:
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG __stdcall Release() override {
        LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    HRESULT __stdcall CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        Log(L"SpikeClassFactory::CreateInstance called");
        HRESULT hr = S_OK;
        CUnknown* punk = SpikePushFilter::CreateInstance(pUnkOuter, &hr);
        if (FAILED(hr) || !punk) { *ppv = nullptr; return FAILED(hr) ? hr : E_FAIL; }
        punk->NonDelegatingAddRef();
        hr = punk->NonDelegatingQueryInterface(riid, ppv);
        punk->NonDelegatingRelease();
        return hr;
    }
    HRESULT __stdcall LockServer(BOOL) override { return S_OK; }
private:
    LONG refs_ = 1;
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    Log(L"DllGetClassObject called");
    if (!IsEqualCLSID(rclsid, CLSID_SpikePushFilter)) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new SpikeClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}
STDAPI DllCanUnloadNow() { return S_OK; }

// ==================== registration ====================
static bool RegisterSelf() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_SpikePushFilter, clsidStr, 64);

    std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr + L"\\InProcServer32";
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        std::wcerr << L"RegCreateKeyExW (InProcServer32) failed" << std::endl;
        return false;
    }
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Both", 5 * sizeof(wchar_t));
    RegCloseKey(hKey);
    Log(L"RegisterSelf: CLSID registry keys written");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInit = SUCCEEDED(hr);
    Log(L"RegisterSelf: CoInitializeEx hr=0x" + std::to_wstring(hr));

    IFilterMapper2* pFM2 = nullptr;
    hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, (void**)&pFM2);
    Log(L"RegisterSelf: CoCreateInstance(FilterMapper2) hr=0x" + std::to_wstring(hr) + L" pFM2=" + std::to_wstring((uintptr_t)pFM2));
    if (FAILED(hr)) {
        std::wcerr << L"CoCreateInstance(FilterMapper2) failed: 0x" << std::hex << hr << std::endl;
        if (comInit) CoUninitialize();
        return false;
    }

    Log(L"RegisterSelf: about to call RegisterFilter");
    REGFILTERPINS regPin = {};
    regPin.strName = nullptr;
    regPin.bRendered = FALSE;
    regPin.bOutput = TRUE;
    regPin.bZero = FALSE;
    regPin.bMany = FALSE;
    regPin.clsConnectsToFilter = nullptr;
    regPin.strConnectsToPin = nullptr;
    regPin.nMediaTypes = 0;
    regPin.lpMediaType = nullptr;

    REGFILTER2 rf2 = {};
    rf2.dwVersion = 1;
    rf2.dwMerit = MERIT_DO_NOT_USE + 1;
    rf2.cPins = 1;
    rf2.rgPins = &regPin;

    hr = pFM2->RegisterFilter(CLSID_SpikePushFilter, kFriendlyName, nullptr,
                                &CLSID_VideoInputDeviceCategory, nullptr, &rf2);
    Log(L"RegisterSelf: RegisterFilter returned hr=0x" + std::to_wstring(hr));
    pFM2->Release();
    Log(L"RegisterSelf: pFM2 released");
    if (comInit) CoUninitialize();
    Log(L"RegisterSelf: CoUninitialize done");

    if (FAILED(hr)) {
        std::wcerr << L"RegisterFilter failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    std::wcout << L"Registered filter under CLSID " << clsidStr << L" as '" << kFriendlyName << L"'" << std::endl;
    return true;
}

static bool UnregisterSelf() {
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_SpikePushFilter, clsidStr, 64);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInit = SUCCEEDED(hr);
    IFilterMapper2* pFM2 = nullptr;
    hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, (void**)&pFM2);
    if (SUCCEEDED(hr)) {
        pFM2->UnregisterFilter(&CLSID_VideoInputDeviceCategory, nullptr, CLSID_SpikePushFilter);
        pFM2->Release();
    }
    if (comInit) CoUninitialize();

    std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr + L"\\InProcServer32";
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, keyPath.c_str());
    std::wstring clsidKeyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr;
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, clsidKeyPath.c_str());

    std::wcout << L"Unregistered." << std::endl;
    return true;
}

// ==================== -enumtest ====================
static int RunEnumTest() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { std::wcerr << L"CoInitializeEx failed" << std::endl; return 1; }

    ICreateDevEnum* pDevEnum = nullptr;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr)) { std::wcerr << L"CoCreateInstance(SystemDeviceEnum) failed: 0x" << std::hex << hr << std::endl; return 1; }

    IEnumMoniker* pEnum = nullptr;
    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    pDevEnum->Release();
    if (hr != S_OK || !pEnum) {
        std::wcerr << L"No video capture devices found at all (hr=0x" << std::hex << hr << L")" << std::endl;
        return 1;
    }

    IMoniker* pMoniker = nullptr;
    IBaseFilter* pFilter = nullptr;
    bool found = false;
    while (pEnum->Next(1, &pMoniker, nullptr) == S_OK) {
        IPropertyBag* pBag = nullptr;
        if (SUCCEEDED(pMoniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)&pBag))) {
            VARIANT var;
            VariantInit(&var);
            if (SUCCEEDED(pBag->Read(L"FriendlyName", &var, nullptr))) {
                std::wcout << L"Found device: " << var.bstrVal << std::endl;
                if (wcscmp(var.bstrVal, kFriendlyName) == 0) {
                    found = true;
                    HRESULT bindHr = pMoniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, (void**)&pFilter);
                    std::wcout << L"  BindToObject: 0x" << std::hex << bindHr << std::endl;
                    Log(L"BindToObject hr=0x" + std::to_wstring(bindHr));
                }
                VariantClear(&var);
            }
            pBag->Release();
        }
        pMoniker->Release();
        if (found) break;
    }
    pEnum->Release();

    if (!found || !pFilter) {
        std::wcout << L"Filter not found in enumeration." << std::endl;
        CoUninitialize();
        return 1;
    }

    // Build a minimal graph: our filter -> Null Renderer, and run it briefly.
    IGraphBuilder* pGraph = nullptr;
    hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&pGraph);
    if (FAILED(hr)) { std::wcerr << L"CoCreateInstance(FilterGraph) failed" << std::endl; return 1; }

    hr = pGraph->AddFilter(pFilter, L"SpikeSource");
    std::wcout << L"AddFilter(source): 0x" << std::hex << hr << std::endl;

    IBaseFilter* pNullRenderer = nullptr;
    hr = CoCreateInstance(CLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&pNullRenderer);
    if (SUCCEEDED(hr)) {
        pGraph->AddFilter(pNullRenderer, L"NullRenderer");
    }

    // Skip ICaptureGraphBuilder2::RenderStream for this spike -- it looks
    // pins up via IKsPropertySet/PIN_CATEGORY_CAPTURE, which isn't
    // implemented yet (that's real-app work, not needed to prove the core
    // FillBuffer pipeline). Connect the output pin directly instead.
    IEnumPins* pEnumPins = nullptr;
    hr = pFilter->EnumPins(&pEnumPins);
    if (SUCCEEDED(hr)) {
        IPin* pOutPin = nullptr;
        if (pEnumPins->Next(1, &pOutPin, nullptr) == S_OK) {
            hr = pGraph->Render(pOutPin);
            std::wcout << L"Render(outputPin): 0x" << std::hex << hr << std::endl;
            Log(L"Render(outputPin) hr=0x" + std::to_wstring(hr));
            pOutPin->Release();
        }
        pEnumPins->Release();
    }

    IMediaControl* pControl = nullptr;
    pGraph->QueryInterface(IID_IMediaControl, (void**)&pControl);
    if (pControl) {
        hr = pControl->Run();
        std::wcout << L"Run: 0x" << std::hex << hr << std::endl;
        Log(L"Graph Run hr=0x" + std::to_wstring(hr));

        Sleep(3000);

        std::wcout << L"FillBuffer call count after 3s: " << g_fillBufferCalls.load() << std::endl;
        Log(L"FillBuffer call count after 3s: " + std::to_wstring(g_fillBufferCalls.load()));

        pControl->Stop();
        pControl->Release();
    }

    if (pNullRenderer) pNullRenderer->Release();
    pFilter->Release();
    pGraph->Release();
    CoUninitialize();
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcout << L"Usage: ds_spike.exe -register|-unregister|-enumtest" << std::endl;
        return 1;
    }
    std::wstring mode = argv[1];
    if (mode == L"-register") return RegisterSelf() ? 0 : 1;
    if (mode == L"-unregister") return UnregisterSelf() ? 0 : 1;
    if (mode == L"-enumtest") return RunEnumTest();
    std::wcerr << L"Unknown mode" << std::endl;
    return 1;
}

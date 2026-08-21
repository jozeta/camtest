#include "VCamEngine.h"
#include "../AppConfig.h"
#include "../Imaging/FrameBuffer.h"
#include <dshow.h>
#include <wrl/client.h>
#include <sstream>
#include <iomanip>

using Microsoft::WRL::ComPtr;

namespace {

std::wstring ClsidToString(const CLSID& clsid) {
    wchar_t buf[64];
    StringFromGUID2(clsid, buf, 64);
    return buf;
}

bool RegisterSlotComClass(int slot, const std::wstring& dllPath, const std::wstring& friendlyName,
                           UINT32 frameWidth, UINT32 frameHeight, std::wstring& outError) {
    std::wstring clsidStr = ClsidToString(VCamSlots::ClsidForSlot(slot));
    std::wstring keyPath = L"Software\\Classes\\CLSID\\" + clsidStr + L"\\InProcServer32";

    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        outError = L"Could not write registry key for slot " + std::to_wstring(slot);
        return false;
    }
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)dllPath.c_str(), (DWORD)((dllPath.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Both", 5 * sizeof(wchar_t));
    RegCloseKey(hKey);

    std::wstring clsidKeyPath = L"Software\\Classes\\CLSID\\" + clsidStr;
    HKEY hKey2;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsidKeyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey2, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey2, nullptr, 0, REG_SZ, (const BYTE*)friendlyName.c_str(), (DWORD)((friendlyName.size() + 1) * sizeof(wchar_t)));
        // Read synchronously by CamtestOutputPin's constructor -- see the
        // per-slot-resolution design note there. Written here (rather than
        // sent live over the pipe) precisely because DirectShow queries
        // GetMediaType before the pipe connection can possibly exist.
        DWORD w = frameWidth, h = frameHeight;
        RegSetValueExW(hKey2, L"FrameWidth", 0, REG_DWORD, (const BYTE*)&w, sizeof(w));
        RegSetValueExW(hKey2, L"FrameHeight", 0, REG_DWORD, (const BYTE*)&h, sizeof(h));
        RegCloseKey(hKey2);
    }
    return true;
}

std::wstring EffectiveNameForSlot(const AppConfig& cfg, int slot) {
    return cfg.cameraNames[slot].empty() ? VCamSlots::FriendlyNameForSlot(slot) : cfg.cameraNames[slot];
}

UINT32 EffectiveWidthForSlot(const AppConfig& cfg, int slot) {
    return cfg.frameWidth[slot] != 0 ? cfg.frameWidth[slot] : FrameBuffer::kDefaultWidth;
}

UINT32 EffectiveHeightForSlot(const AppConfig& cfg, int slot) {
    return cfg.frameHeight[slot] != 0 ? cfg.frameHeight[slot] : FrameBuffer::kDefaultHeight;
}

void UnregisterSlotComClass(int slot) {
    std::wstring clsidStr = ClsidToString(VCamSlots::ClsidForSlot(slot));
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, (L"Software\\Classes\\CLSID\\" + clsidStr + L"\\InProcServer32").c_str());
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, (L"Software\\Classes\\CLSID\\" + clsidStr).c_str());
}

} // namespace

VCamEngine::VCamEngine() = default;

VCamEngine::~VCamEngine() {
    for (int i = 0; i < VCamSlots::kMaxSlots; ++i) StopSlot(i);
}

bool VCamEngine::RegisterAllSlots(std::wstring& outError) {
    std::wstring dllPath = GetFilterDllPath();
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        outError = L"camtest_filter.dll not found next to camtest.exe (expected at " + dllPath + L")";
        return false;
    }

    AppConfig cfg = AppConfig::Load();

    for (int slot = 0; slot < VCamSlots::kMaxSlots; ++slot) {
        if (!RegisterSlotComClass(slot, dllPath, EffectiveNameForSlot(cfg, slot),
                                   EffectiveWidthForSlot(cfg, slot), EffectiveHeightForSlot(cfg, slot), outError)) {
            return false;
        }
    }

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInit = SUCCEEDED(coHr);

    ComPtr<IFilterMapper2> pFM2;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, (void**)pFM2.GetAddressOf());
    if (FAILED(hr)) {
        outError = L"Could not create the DirectShow filter mapper (0x" + std::to_wstring((unsigned)hr) + L")";
        if (comInit) CoUninitialize();
        return false;
    }

    for (int slot = 0; slot < VCamSlots::kMaxSlots; ++slot) {
        // A NULL REGFILTER2* crashes RegisterFilter on current Windows
        // (confirmed via spike testing) -- a minimal one-pin descriptor is
        // required even though we don't need rich pin/media-type metadata
        // here (that's negotiated live via CheckMediaType/GetMediaType).
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

        std::wstring friendly = EffectiveNameForSlot(cfg, slot);
        hr = pFM2->RegisterFilter(VCamSlots::ClsidForSlot(slot), friendly.c_str(), nullptr,
                                    &CLSID_VideoInputDeviceCategory, nullptr, &rf2);
        if (FAILED(hr)) {
            outError = L"Could not register camera slot " + std::to_wstring(slot + 1) +
                       L" as a capture device (0x" + std::to_wstring((unsigned)hr) + L")";
            if (comInit) CoUninitialize();
            return false;
        }
    }

    if (comInit) CoUninitialize();

    cfg.registered = true;
    cfg.registeredExePath = dllPath;
    cfg.Save();

    return true;
}

bool VCamEngine::UnregisterAllSlots(std::wstring& outError) {
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInit = SUCCEEDED(coHr);

    ComPtr<IFilterMapper2> pFM2;
    if (SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_IFilterMapper2, (void**)pFM2.GetAddressOf()))) {
        for (int slot = 0; slot < VCamSlots::kMaxSlots; ++slot) {
            pFM2->UnregisterFilter(&CLSID_VideoInputDeviceCategory, nullptr, VCamSlots::ClsidForSlot(slot));
        }
    }
    if (comInit) CoUninitialize();

    for (int slot = 0; slot < VCamSlots::kMaxSlots; ++slot) {
        UnregisterSlotComClass(slot);
    }

    AppConfig cfg = AppConfig::Load();
    cfg.registered = false;
    cfg.registeredExePath.clear();
    cfg.Save();

    outError.clear();
    return true;
}

bool VCamEngine::StartSlot(int slot, const std::wstring& imagePath) {
    if (servers_[slot]) return true; // already running
    auto server = std::make_unique<NamedPipeServer>(slot);
    if (!server->StartListening()) return false;
    server->SetImagePath(imagePath);
    servers_[slot] = std::move(server);
    return true;
}

void VCamEngine::StopSlot(int slot) {
    servers_[slot].reset();
}

void VCamEngine::SetImagePath(int slot, const std::wstring& imagePath) {
    if (servers_[slot]) servers_[slot]->SetImagePath(imagePath);
}

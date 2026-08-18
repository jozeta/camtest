// Throwaway spike: proves (or disproves) that a single .exe can act as its
// own in-proc COM server for Windows' Camera Frame Server / Media
// Foundation virtual camera activation path, which is the packaging trick
// the real app depends on for a true single-file exe.
//
// Round 1 (kept for reference in comments below) showed:
//   - MFCreateVirtualCamera + Start() DOES CoCreateInstance our registered
//     CLSID synchronously, IN OUR OWN CALLING PROCESS (not a separate
//     svchost), requesting IID_IMFActivate (GUID {7FEE9E9A-...}) --
//     confirming the standard MF device-activation pattern: the class
//     object must hand back an IMFActivate wrapper, whose ActivateObject()
//     lazily constructs the real IMFMediaSource.
//
// This round implements a minimal-but-real IMFActivate + IMFMediaSource +
// IMFMediaStream (static color-frame NV12 source, no image loading yet --
// that's wired up in the real app) to confirm the full activation chain
// succeeds, i.e. IMFVirtualCamera::Start() returns S_OK.
//
// Modes (see wmain):
//   -register     write HKLM CLSID\{...}\InProcServer32 pointing at this exe (needs admin)
//   -unregister   remove those keys (needs admin)
//   -createcam    create+start a session-lifetime virtual camera backed by our CLSID,
//                 then sleep to allow inspection / a real consumer to open it
//   -activate     enumerate video capture devices, find "Spike Test Camera", and
//                 activate it directly (bypassing IMFVirtualCamera) as an extra check

#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <mferror.h>
#include <wrl/client.h>

// ks.h's real IKsControl COM declaration is gated behind an
// __IUnknown_INTERFACE_DEFINED__ check that never ends up true in this
// translation unit (confirmed via preprocessor dump -- a header-ordering
// quirk in this SDK), even though the underlying KSPROPERTY/KSMETHOD/
// KSEVENT struct types it depends on ARE available unconditionally.
// Declare it ourselves, ABI-matching ks.h's version, rather than fight it.
MIDL_INTERFACE("28F54685-06FD-11D2-B27A-00A0C9223196")
IKsControl : public IUnknown {
public:
    virtual NTSTATUS STDMETHODCALLTYPE KsProperty(
        PKSPROPERTY Property, ULONG PropertyLength,
        LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned) = 0;
    virtual NTSTATUS STDMETHODCALLTYPE KsMethod(
        PKSMETHOD Method, ULONG MethodLength,
        LPVOID MethodData, ULONG DataLength, ULONG* BytesReturned) = 0;
    virtual NTSTATUS STDMETHODCALLTYPE KsEvent(
        PKSEVENT Event, ULONG EventLength,
        LPVOID EventData, ULONG DataLength, ULONG* BytesReturned) = 0;
};
static const IID IID_IKsControl = __uuidof(IKsControl);
#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfsensorgroup.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

// {A1B2C3D4-E5F6-4A5B-8C9D-0102030405AA} -- private test CLSID, not registered anywhere else.
static const CLSID CLSID_SpikeTestSource = {
    0xA1B2C3D4, 0xE5F6, 0x4A5B, { 0x8C, 0x9D, 0x01, 0x02, 0x03, 0x04, 0x05, 0xAA }
};
static const wchar_t* kClsidString = L"{A1B2C3D4-E5F6-4A5B-8C9D-0102030405AA}";
static const wchar_t* kFriendlyName = L"Spike Test Camera";

static std::wstring ExeDir() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring p = exePath;
    size_t slash = p.find_last_of(L"\\/");
    return p.substr(0, slash);
}

static void Log(const std::wstring& msg) {
    std::wofstream f(ExeDir() + L"\\spike_log.txt", std::ios::app);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    f << buf << L"PID=" << GetCurrentProcessId() << L" " << msg << std::endl;
    f.close();
}

static void LogHr(const wchar_t* what, HRESULT hr) {
    wchar_t buf[256];
    swprintf_s(buf, L"%s hr=0x%08X", what, (unsigned)hr);
    Log(buf);
}

// ==================== minimal ref-counted IUnknown base ====================
template <typename... Interfaces>
class UnknownBase : public Interfaces... {
public:
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG __stdcall Release() override {
        LONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return (ULONG)r;
    }
protected:
    virtual ~UnknownBase() = default;
    LONG refs_ = 1;
};

// ==================== IMFMediaStream (one video stream, static frame) ====================
class SpikeMediaStream : public UnknownBase<IMFMediaStream> {
public:
    SpikeMediaStream() {
        MFCreateEventQueue(&eventQueue_);
    }

    bool Initialize(IMFMediaSource* source, IMFStreamDescriptor* sd) {
        source_ = source; // weak-ish; source outlives stream in our lifecycle
        sd_ = sd;
        return true;
    }

    void StartPump() {
        stopping_ = false;
        pumpThread_ = std::thread([this] { PumpLoop(); });
    }

    void StopPump() {
        stopping_ = true;
        if (pumpThread_.joinable()) pumpThread_.join();
        if (eventQueue_) eventQueue_->Shutdown();
    }

    // ---- IUnknown ----
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IMFMediaStream || riid == IID_IMFMediaEventGenerator) {
            *ppv = static_cast<IMFMediaStream*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }

    // ---- IMFMediaEventGenerator ----
    HRESULT __stdcall GetEvent(DWORD flags, IMFMediaEvent** ppEvent) override {
        return eventQueue_->GetEvent(flags, ppEvent);
    }
    HRESULT __stdcall BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) override {
        return eventQueue_->BeginGetEvent(cb, state);
    }
    HRESULT __stdcall EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** ppEvent) override {
        return eventQueue_->EndGetEvent(result, ppEvent);
    }
    HRESULT __stdcall QueueEvent(MediaEventType met, REFGUID extType, HRESULT status, const PROPVARIANT* value) override {
        return eventQueue_->QueueEventParamVar(met, extType, status, value);
    }

    // ---- IMFMediaStream ----
    HRESULT __stdcall GetMediaSource(IMFMediaSource** ppSource) override {
        *ppSource = source_;
        (*ppSource)->AddRef();
        return S_OK;
    }
    HRESULT __stdcall GetStreamDescriptor(IMFStreamDescriptor** ppSd) override {
        *ppSd = sd_.Get();
        (*ppSd)->AddRef();
        return S_OK;
    }
    HRESULT __stdcall RequestSample(IUnknown* pToken) override {
        std::lock_guard<std::mutex> lock(tokenMutex_);
        pendingTokens_.push_back(pToken);
        if (pToken) pToken->AddRef();
        return S_OK;
    }

private:
    void PumpLoop() {
        LONGLONG frameIndex = 0;
        const LONGLONG frameDuration = 333333; // 100ns units, ~30fps
        while (!stopping_) {
            IUnknown* token = nullptr;
            {
                std::lock_guard<std::mutex> lock(tokenMutex_);
                if (!pendingTokens_.empty()) {
                    token = pendingTokens_.front();
                    pendingTokens_.erase(pendingTokens_.begin());
                }
            }
            if (!token) { Sleep(10); continue; }

            ComPtr<IMFSample> sample;
            HRESULT hr = MFCreateSample(&sample);
            if (SUCCEEDED(hr)) {
                ComPtr<IMFMediaBuffer> buffer;
                const DWORD size = 1920 * 1080 * 3 / 2;
                hr = MFCreateMemoryBuffer(size, &buffer);
                if (SUCCEEDED(hr)) {
                    BYTE* data = nullptr;
                    buffer->Lock(&data, nullptr, nullptr);
                    memset(data, 128, size); // flat gray NV12 test pattern
                    buffer->Unlock();
                    buffer->SetCurrentLength(size);
                    sample->AddBuffer(buffer.Get());
                    sample->SetSampleTime(frameIndex * frameDuration);
                    sample->SetSampleDuration(frameDuration);
                    ++frameIndex;

                    PROPVARIANT var;
                    PropVariantInit(&var);
                    var.vt = VT_UNKNOWN;
                    var.punkVal = sample.Get();
                    sample->AddRef();
                    QueueEvent(MEMediaSample, GUID_NULL, S_OK, &var);
                    PropVariantClear(&var);
                }
            }
            token->Release();
        }
    }

    ComPtr<IMFMediaEventQueue> eventQueue_;
    IMFMediaSource* source_ = nullptr;
    ComPtr<IMFStreamDescriptor> sd_;
    std::thread pumpThread_;
    std::atomic<bool> stopping_{false};
    std::mutex tokenMutex_;
    std::vector<IUnknown*> pendingTokens_;
};

// ==================== IMFMediaSource ====================
class SpikeMediaSource : public UnknownBase<IMFMediaSourceEx, IMFGetService, IKsControl> {
public:
    SpikeMediaSource() {
        MFCreateEventQueue(&eventQueue_);
    }
    ~SpikeMediaSource() override {
        if (stream_) stream_->StopPump();
    }

    HRESULT Initialize() {
        HRESULT hr = MFCreateMediaType(&mediaType_);
        if (FAILED(hr)) return hr;
        mediaType_->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        mediaType_->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        MFSetAttributeSize(mediaType_.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
        MFSetAttributeRatio(mediaType_.Get(), MF_MT_FRAME_RATE, 30, 1);
        MFSetAttributeRatio(mediaType_.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        mediaType_->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        mediaType_->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        mediaType_->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
        mediaType_->SetUINT32(MF_MT_SAMPLE_SIZE, 1920 * 1080 * 3 / 2);
        // NV12 is 1.5 bytes/pixel; matches the official sample's pattern of
        // setting AVG_BITRATE (width*height*bytesPerPixel*8*fps).
        mediaType_->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)(1920ULL * 1080 * 3 / 2 * 8 * 30));

        ComPtr<IMFMediaType> rgb32Type;
        hr = MFCreateMediaType(&rgb32Type);
        if (FAILED(hr)) return hr;
        rgb32Type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        rgb32Type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        MFSetAttributeSize(rgb32Type.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
        MFSetAttributeRatio(rgb32Type.Get(), MF_MT_FRAME_RATE, 30, 1);
        MFSetAttributeRatio(rgb32Type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        rgb32Type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        rgb32Type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        rgb32Type->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)(1920ULL * 1080 * 4 * 8 * 30));

        IMFMediaType* types[2] = { mediaType_.Get(), rgb32Type.Get() };
        hr = MFCreateStreamDescriptor(0, 2, types, &streamDescriptor_);
        if (FAILED(hr)) return hr;

        ComPtr<IMFMediaTypeHandler> handler;
        hr = streamDescriptor_->GetMediaTypeHandler(&handler);
        if (FAILED(hr)) return hr;
        hr = handler->SetCurrentMediaType(mediaType_.Get());
        if (FAILED(hr)) return hr;

        stream_ = new SpikeMediaStream();
        stream_->Initialize(this, streamDescriptor_.Get());

        hr = MFCreateAttributes(&streamAttributes_, 4);
        if (FAILED(hr)) return hr;
        streamAttributes_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
        streamAttributes_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
        streamAttributes_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
        streamAttributes_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);

        // Source-level attributes: MUST include a sensor profile collection
        // -- Frame Server (running out-of-process) reads this when
        // registering the device, and its absence causes Frame Server
        // itself to crash rather than fail gracefully (confirmed via
        // Application event log during spike testing).
        hr = MFCreateAttributes(&sourceAttributes_, 1);
        if (FAILED(hr)) return hr;

        ComPtr<IMFSensorProfileCollection> profileCollection;
        ComPtr<IMFSensorProfile> profile;
        hr = MFCreateSensorProfileCollection(&profileCollection);
        if (FAILED(hr)) return hr;
        hr = MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &profile);
        if (FAILED(hr)) return hr;
        hr = profile->AddProfileFilter(0, L"((RES==;FRT<=30,1;SUT==))");
        if (FAILED(hr)) return hr;
        hr = profileCollection->AddProfile(profile.Get());
        if (FAILED(hr)) return hr;
        hr = sourceAttributes_->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, profileCollection.Get());
        if (FAILED(hr)) return hr;

        return S_OK;
    }

    // ---- IUnknown ----
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IMFMediaSource || riid == IID_IMFMediaSourceEx || riid == IID_IMFMediaEventGenerator) {
            *ppv = static_cast<IMFMediaSourceEx*>(this); AddRef(); return S_OK;
        }
        if (riid == IID_IMFGetService) {
            *ppv = static_cast<IMFGetService*>(this); AddRef(); return S_OK;
        }
        if (riid == IID_IKsControl) {
            *ppv = static_cast<IKsControl*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }

    // ---- IMFGetService ----
    HRESULT __stdcall GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject) override {
        *ppvObject = nullptr;
        return MF_E_UNSUPPORTED_SERVICE;
    }

    // ---- IKsControl ----
    static constexpr NTSTATUS kStatusNotImplemented = (NTSTATUS)0xC0000002L;
    NTSTATUS __stdcall KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG* BytesReturned) override {
        *BytesReturned = 0;
        return kStatusNotImplemented;
    }
    NTSTATUS __stdcall KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG* BytesReturned) override {
        *BytesReturned = 0;
        return kStatusNotImplemented;
    }
    NTSTATUS __stdcall KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG* BytesReturned) override {
        if (BytesReturned) *BytesReturned = 0;
        return kStatusNotImplemented;
    }

    // ---- IMFMediaSourceEx ----
    HRESULT __stdcall GetSourceAttributes(IMFAttributes** ppAttributes) override {
        *ppAttributes = sourceAttributes_.Get();
        (*ppAttributes)->AddRef();
        return S_OK;
    }
    HRESULT __stdcall GetStreamAttributes(DWORD, IMFAttributes** ppAttributes) override {
        *ppAttributes = streamAttributes_.Get();
        (*ppAttributes)->AddRef();
        return S_OK;
    }
    HRESULT __stdcall SetD3DManager(IUnknown*) override {
        return S_OK; // no D3D acceleration needed for a static-image source
    }

    // ---- IMFMediaEventGenerator ----
    HRESULT __stdcall GetEvent(DWORD flags, IMFMediaEvent** ppEvent) override {
        return eventQueue_->GetEvent(flags, ppEvent);
    }
    HRESULT __stdcall BeginGetEvent(IMFAsyncCallback* cb, IUnknown* state) override {
        return eventQueue_->BeginGetEvent(cb, state);
    }
    HRESULT __stdcall EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** ppEvent) override {
        return eventQueue_->EndGetEvent(result, ppEvent);
    }
    HRESULT __stdcall QueueEvent(MediaEventType met, REFGUID extType, HRESULT status, const PROPVARIANT* value) override {
        return eventQueue_->QueueEventParamVar(met, extType, status, value);
    }

    // ---- IMFMediaSource ----
    HRESULT __stdcall GetCharacteristics(DWORD* pdw) override {
        *pdw = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }
    HRESULT __stdcall CreatePresentationDescriptor(IMFPresentationDescriptor** ppPd) override {
        if (!presentationDescriptor_) {
            IMFStreamDescriptor* sds[1] = { streamDescriptor_.Get() };
            HRESULT hr = MFCreatePresentationDescriptor(1, sds, &presentationDescriptor_);
            if (FAILED(hr)) return hr;
            presentationDescriptor_->SelectStream(0);
        }
        return presentationDescriptor_->Clone(ppPd);
    }
    HRESULT __stdcall Start(IMFPresentationDescriptor* pd, const GUID* timeFormat, const PROPVARIANT* startPos) override {
        Log(L"SpikeMediaSource::Start called");
        stream_->StartPump();

        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_UNKNOWN;
        var.punkVal = static_cast<IMFMediaStream*>(stream_);
        stream_->AddRef();
        QueueEvent(MENewStream, GUID_NULL, S_OK, &var);
        PropVariantClear(&var);

        PROPVARIANT startVar;
        PropVariantInit(&startVar);
        startVar.vt = VT_I8;
        startVar.hVal.QuadPart = 0;
        stream_->QueueEvent(MEStreamStarted, GUID_NULL, S_OK, &startVar);
        QueueEvent(MESourceStarted, GUID_NULL, S_OK, &startVar);
        PropVariantClear(&startVar);

        return S_OK;
    }
    HRESULT __stdcall Stop() override {
        Log(L"SpikeMediaSource::Stop called");
        if (stream_) stream_->StopPump();
        QueueEvent(MESourceStopped, GUID_NULL, S_OK, nullptr);
        return S_OK;
    }
    HRESULT __stdcall Pause() override { return MF_E_INVALID_STATE_TRANSITION; }
    HRESULT __stdcall Shutdown() override {
        Log(L"SpikeMediaSource::Shutdown called");
        if (stream_) stream_->StopPump();
        if (eventQueue_) eventQueue_->Shutdown();
        return S_OK;
    }

private:
    ComPtr<IMFMediaEventQueue> eventQueue_;
    ComPtr<IMFMediaType> mediaType_;
    ComPtr<IMFStreamDescriptor> streamDescriptor_;
    ComPtr<IMFPresentationDescriptor> presentationDescriptor_;
    ComPtr<IMFAttributes> streamAttributes_;
    ComPtr<IMFAttributes> sourceAttributes_;
    SpikeMediaStream* stream_ = nullptr; // owns one ref via new; released in real impl -- fine for spike
};

// ==================== IMFActivate wrapper ====================
// IMFActivate derives from IMFAttributes; we delegate every IMFAttributes
// method to a real attribute store rather than implementing ~20 methods by
// hand.
class SpikeActivate : public UnknownBase<IMFActivate> {
public:
    HRESULT Initialize() {
        return MFCreateAttributes(&attrs_, 4);
    }

    // ---- IUnknown ----
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IMFAttributes || riid == IID_IMFActivate) {
            *ppv = static_cast<IMFActivate*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }

    // ---- IMFAttributes (delegate) ----
    HRESULT __stdcall GetItem(REFGUID k, PROPVARIANT* v) override { return attrs_->GetItem(k, v); }
    HRESULT __stdcall GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return attrs_->GetItemType(k, t); }
    HRESULT __stdcall CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return attrs_->CompareItem(k, v, r); }
    HRESULT __stdcall Compare(IMFAttributes* o, MF_ATTRIBUTES_MATCH_TYPE t, BOOL* r) override { return attrs_->Compare(o, t, r); }
    HRESULT __stdcall GetUINT32(REFGUID k, UINT32* v) override { return attrs_->GetUINT32(k, v); }
    HRESULT __stdcall GetUINT64(REFGUID k, UINT64* v) override { return attrs_->GetUINT64(k, v); }
    HRESULT __stdcall GetDouble(REFGUID k, double* v) override { return attrs_->GetDouble(k, v); }
    HRESULT __stdcall GetGUID(REFGUID k, GUID* v) override { return attrs_->GetGUID(k, v); }
    HRESULT __stdcall GetStringLength(REFGUID k, UINT32* l) override { return attrs_->GetStringLength(k, l); }
    HRESULT __stdcall GetString(REFGUID k, LPWSTR buf, UINT32 sz, UINT32* len) override { return attrs_->GetString(k, buf, sz, len); }
    HRESULT __stdcall GetAllocatedString(REFGUID k, LPWSTR* buf, UINT32* len) override { return attrs_->GetAllocatedString(k, buf, len); }
    HRESULT __stdcall GetBlobSize(REFGUID k, UINT32* sz) override { return attrs_->GetBlobSize(k, sz); }
    HRESULT __stdcall GetBlob(REFGUID k, UINT8* buf, UINT32 sz, UINT32* got) override { return attrs_->GetBlob(k, buf, sz, got); }
    HRESULT __stdcall GetAllocatedBlob(REFGUID k, UINT8** buf, UINT32* sz) override { return attrs_->GetAllocatedBlob(k, buf, sz); }
    HRESULT __stdcall GetUnknown(REFGUID k, REFIID riid, LPVOID* ppv) override { return attrs_->GetUnknown(k, riid, ppv); }
    HRESULT __stdcall SetItem(REFGUID k, REFPROPVARIANT v) override { return attrs_->SetItem(k, v); }
    HRESULT __stdcall DeleteItem(REFGUID k) override { return attrs_->DeleteItem(k); }
    HRESULT __stdcall DeleteAllItems() override { return attrs_->DeleteAllItems(); }
    HRESULT __stdcall SetUINT32(REFGUID k, UINT32 v) override { return attrs_->SetUINT32(k, v); }
    HRESULT __stdcall SetUINT64(REFGUID k, UINT64 v) override { return attrs_->SetUINT64(k, v); }
    HRESULT __stdcall SetDouble(REFGUID k, double v) override { return attrs_->SetDouble(k, v); }
    HRESULT __stdcall SetGUID(REFGUID k, REFGUID v) override { return attrs_->SetGUID(k, v); }
    HRESULT __stdcall SetString(REFGUID k, LPCWSTR v) override { return attrs_->SetString(k, v); }
    HRESULT __stdcall SetBlob(REFGUID k, const UINT8* buf, UINT32 sz) override { return attrs_->SetBlob(k, buf, sz); }
    HRESULT __stdcall SetUnknown(REFGUID k, IUnknown* v) override { return attrs_->SetUnknown(k, v); }
    HRESULT __stdcall LockStore() override { return attrs_->LockStore(); }
    HRESULT __stdcall UnlockStore() override { return attrs_->UnlockStore(); }
    HRESULT __stdcall GetCount(UINT32* c) override { return attrs_->GetCount(c); }
    HRESULT __stdcall GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return attrs_->GetItemByIndex(i, k, v); }
    HRESULT __stdcall CopyAllItems(IMFAttributes* dest) override { return attrs_->CopyAllItems(dest); }

    // ---- IMFActivate ----
    HRESULT __stdcall ActivateObject(REFIID riid, void** ppv) override {
        wchar_t riidStr[64];
        StringFromGUID2(riid, riidStr, 64);
        Log(std::wstring(L"SpikeActivate::ActivateObject called, riid=") + riidStr);

        if (!source_) {
            source_ = new SpikeMediaSource();
            HRESULT hr = source_->Initialize();
            if (FAILED(hr)) {
                LogHr(L"SpikeMediaSource::Initialize", hr);
                source_->Release();
                source_ = nullptr;
                return hr;
            }
        }
        HRESULT hr = source_->QueryInterface(riid, ppv);
        LogHr(L"ActivateObject QueryInterface result", hr);
        return hr;
    }
    HRESULT __stdcall ShutdownObject() override {
        Log(L"SpikeActivate::ShutdownObject called");
        if (source_) { source_->Shutdown(); source_->Release(); source_ = nullptr; }
        return S_OK;
    }
    HRESULT __stdcall DetachObject() override {
        Log(L"SpikeActivate::DetachObject called");
        source_ = nullptr;
        return S_OK;
    }

private:
    ComPtr<IMFAttributes> attrs_;
    SpikeMediaSource* source_ = nullptr;
};

// ==================== minimal IClassFactory ====================
class SpikeClassFactory : public UnknownBase<IClassFactory> {
public:
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    HRESULT __stdcall CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        wchar_t riidStr[64];
        StringFromGUID2(riid, riidStr, 64);
        Log(std::wstring(L"SpikeClassFactory::CreateInstance called, riid=") + riidStr);

        auto* activate = new SpikeActivate();
        HRESULT hr = activate->Initialize();
        if (FAILED(hr)) { activate->Release(); *ppv = nullptr; return hr; }
        hr = activate->QueryInterface(riid, ppv);
        activate->Release();
        return hr;
    }
    HRESULT __stdcall LockServer(BOOL) override { return S_OK; }
};

// ---------------- exported COM server entry points ----------------
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    Log(L"DllGetClassObject called");
    if (!IsEqualCLSID(rclsid, CLSID_SpikeTestSource)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new SpikeClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    Log(L"DllCanUnloadNow called");
    return S_OK;
}

// ---------------- registration ----------------
static bool RegisterSelf() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString + L"\\InProcServer32";
    HKEY hKey;
    LONG res = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0,
                                KEY_WRITE, nullptr, &hKey, nullptr);
    if (res != ERROR_SUCCESS) {
        std::wcerr << L"RegCreateKeyExW failed: " << res << std::endl;
        return false;
    }
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Both", (DWORD)(5 * sizeof(wchar_t)));
    RegCloseKey(hKey);

    std::wstring clsidKeyPath = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    HKEY hKey2;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsidKeyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey2, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey2, nullptr, 0, REG_SZ, (const BYTE*)kFriendlyName, (DWORD)((wcslen(kFriendlyName) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey2);
    }

    std::wcout << L"Registered " << exePath << L" under CLSID " << kClsidString << std::endl;
    return true;
}

static bool UnregisterSelf() {
    std::wstring clsidKeyPath = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidString;
    std::wstring inprocPath = clsidKeyPath + L"\\InProcServer32";
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, inprocPath.c_str());
    LONG res = RegDeleteKeyW(HKEY_LOCAL_MACHINE, clsidKeyPath.c_str());
    std::wcout << L"Unregister result: " << res << std::endl;
    return true;
}

// ---------------- -createcam ----------------
// IMPORTANT (discovered empirically): MFCreateVirtualCamera/Start() can
// involve a camera-access consent step that requires the CALLING THREAD to
// be pumping window messages -- calling it from a thread with no message
// loop at all (e.g. a bare console app's main thread) reliably fails with
// E_ACCESSDENIED (0x80070005), independent of the Settings > Privacy
// camera toggle already being Allow. Fix: do the MF calls on a background
// thread while the main thread runs a message pump (MsgWaitForMultipleObjects).
// The real app's GUI (Win32 dialog, real message loop) satisfies this
// naturally as long as Start/Stop logic itself still runs off the UI
// thread, matching what's already planned for the async pipe-connect wait.
struct CreateCamResult {
    HRESULT hr = E_FAIL;
    IMFVirtualCamera* cam = nullptr;
};

static void CreateCamWorker(CreateCamResult* result, HANDLE doneEvent) {
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { result->hr = hr; SetEvent(doneEvent); return; }

    IMFVirtualCamera* cam = nullptr;
    hr = MFCreateVirtualCamera(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_Session,
        MFVirtualCameraAccess_CurrentUser,
        kFriendlyName,
        kClsidString,
        nullptr, 0,
        &cam);
    if (FAILED(hr)) {
        LogHr(L"MFCreateVirtualCamera FAILED", hr);
        result->hr = hr;
        SetEvent(doneEvent);
        return;
    }

    hr = cam->Start(nullptr);
    if (FAILED(hr)) {
        LogHr(L"IMFVirtualCamera::Start FAILED", hr);
        cam->Release();
        result->hr = hr;
        SetEvent(doneEvent);
        return;
    }

    Log(L"Virtual camera started OK");
    result->hr = S_OK;
    result->cam = cam;
    SetEvent(doneEvent);
}

static int RunCreateCam() {
    HANDLE doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CreateCamResult result;
    std::thread worker(CreateCamWorker, &result, doneEvent);

    // Pump messages on this (main) thread while the worker does the MF
    // calls, so any thread-affine consent UI Media Foundation needs can
    // actually be serviced.
    for (;;) {
        DWORD w = MsgWaitForMultipleObjects(1, &doneEvent, FALSE, INFINITE, QS_ALLINPUT);
        if (w == WAIT_OBJECT_0) break; // worker signaled completion
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    worker.join();
    CloseHandle(doneEvent);

    if (FAILED(result.hr)) {
        std::wcerr << L"createcam failed: 0x" << std::hex << result.hr << std::endl;
        MFShutdown();
        return 1;
    }

    std::wcout << L"Virtual camera '" << kFriendlyName << L"' created and started." << std::endl;
    std::wcout << L"Sleeping 180s to allow inspection / a real consumer to open it..." << std::endl;

    // Keep pumping messages during the sleep too, in case anything else
    // needs it while the camera is live.
    ULONGLONG until = GetTickCount64() + 180000;
    while (GetTickCount64() < until) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    result.cam->Stop();
    result.cam->Remove();
    result.cam->Release();
    MFShutdown();
    std::wcout << L"Stopped." << std::endl;
    return 0;
}

// ---------------- -activate ----------------
static int RunActivate() {
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { std::wcerr << L"MFStartup failed: 0x" << std::hex << hr << std::endl; return 1; }

    IMFAttributes* attrs = nullptr;
    hr = MFCreateAttributes(&attrs, 1);
    if (SUCCEEDED(hr)) {
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(attrs, &devices, &count);
    if (attrs) attrs->Release();
    if (FAILED(hr)) {
        std::wcerr << L"MFEnumDeviceSources failed: 0x" << std::hex << hr << std::endl;
        MFShutdown();
        return 1;
    }

    std::wcout << L"Found " << count << L" video capture device(s):" << std::endl;
    bool found = false;
    for (UINT32 i = 0; i < count; ++i) {
        wchar_t* name = nullptr;
        UINT32 nameLen = 0;
        devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen);
        std::wcout << L"  [" << i << L"] " << (name ? name : L"(unnamed)") << std::endl;

        if (name && wcscmp(name, kFriendlyName) == 0) {
            found = true;
            std::wcout << L"    -> Activating..." << std::endl;
            Log(L"Test process activating the virtual camera source");
            IMFMediaSource* source = nullptr;
            HRESULT activateHr = devices[i]->ActivateObject(IID_PPV_ARGS(&source));
            std::wcout << L"    ActivateObject returned: 0x" << std::hex << activateHr << std::endl;
            Log(L"ActivateObject (from -activate) returned hr=0x" + std::to_wstring(activateHr));
            if (source) source->Release();
        }
        if (name) CoTaskMemFree(name);
        devices[i]->Release();
    }
    CoTaskMemFree(devices);

    if (!found) {
        std::wcout << L"'" << kFriendlyName << L"' not found in device list -- is -createcam still running?" << std::endl;
    }

    MFShutdown();
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::wcout << L"Usage: spike.exe -register|-unregister|-createcam|-activate" << std::endl;
        return 1;
    }
    std::wstring mode = argv[1];
    if (mode == L"-register") return RegisterSelf() ? 0 : 1;
    if (mode == L"-unregister") return UnregisterSelf() ? 0 : 1;
    if (mode == L"-createcam") return RunCreateCam();
    if (mode == L"-activate") return RunActivate();

    std::wcerr << L"Unknown mode: " << mode << std::endl;
    return 1;
}

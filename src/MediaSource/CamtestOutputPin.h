#pragma once
#include <streams.h>
#include <ks.h>
#include <ksproxy.h>
#include "../Imaging/FrameBuffer.h"
#include "../Ipc/NamedPipeClient.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The single output pin of a camera slot's filter. Pushes a static image or
// an animated GIF's frame sequence (received over the slot's named pipe as
// a path, decoded/letterboxed/converted elsewhere and handed to us as one
// or more immutable FrameBuffers) as a fixed-resolution@30fps RGB24
// stream, following the classic CSourceStream push-source pattern
// (validated end-to-end via a throwaway spike before this was written).
class CamtestOutputPin : public CSourceStream, public IKsPropertySet, public IAMStreamConfig {
public:
    CamtestOutputPin(int slotIndex, HRESULT* phr, CSource* pFilter);
    ~CamtestOutputPin() override;

    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

    // ---- CSourceStream ----
    HRESULT FillBuffer(IMediaSample* pSample) override;
    HRESULT GetMediaType(CMediaType* pmt) override;
    HRESULT CheckMediaType(const CMediaType* pmt) override;
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) override;

    // ---- IKsPropertySet ---- (only need to report PIN_CATEGORY_CAPTURE)
    HRESULT STDMETHODCALLTYPE Set(REFGUID PropSet, ULONG Id, LPVOID InstanceData, ULONG InstanceLength,
                                    LPVOID PropertyData, ULONG DataLength) override;
    HRESULT STDMETHODCALLTYPE Get(REFGUID PropSet, ULONG Id, LPVOID InstanceData, ULONG InstanceLength,
                                    LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned) override;
    HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID PropSet, ULONG Id, ULONG* pTypeSupport) override;

    // ---- IAMStreamConfig ---- (single fixed format, reported for
    // compatibility with consumers that explicitly query/negotiate it)
    HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE* pmt) override;
    HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE** ppmt) override;
    HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* piCount, int* piSize) override;
    HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override;

    // Called by the pipe listener callback when a new image path arrives.
    // Decodes off the calling (listener) thread and atomically swaps the
    // frame the pump reads; no-ops if the path is unchanged from what's
    // already loaded (the server heartbeats the path periodically).
    void OnImagePathReceived(const std::wstring& path);

private:
    int slotIndex_;
    std::unique_ptr<NamedPipeClient> pipeClient_;

    // Fixed for this pin's lifetime, read once at construction from the
    // registry (written by VCamEngine::RegisterAllSlots) -- see the
    // per-slot-resolution design note in CamtestOutputPin.cpp.
    UINT32 width_ = FrameBuffer::kDefaultWidth;
    UINT32 height_ = FrameBuffer::kDefaultHeight;

    struct AnimFrame {
        std::shared_ptr<FrameBuffer> frame;
        DWORD delayMs;
    };

    std::mutex frameMutex_;
    std::vector<AnimFrame> frames_;      // always >=1 once loaded; size==1 is the static-image case
    size_t animIndex_ = 0;
    ULONGLONG nextAnimAdvanceTick_ = 0;  // GetTickCount64() deadline; only meaningful when frames_.size() > 1
    std::wstring lastLoadedPath_;

    LONGLONG frameIndex_ = 0;
    static constexpr REFERENCE_TIME kFrameDuration = 333333; // 100ns units, ~30fps
};

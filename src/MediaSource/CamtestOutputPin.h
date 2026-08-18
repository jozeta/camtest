#pragma once
#include <streams.h>
#include <ks.h>
#include <ksproxy.h>
#include "../Imaging/FrameBuffer.h"
#include "../Ipc/NamedPipeClient.h"
#include <memory>
#include <mutex>
#include <string>

// The single output pin of a camera slot's filter. Pushes a static image
// (received over the slot's named pipe, decoded/letterboxed/converted
// elsewhere and handed to us as an immutable FrameBuffer) as a fixed
// 1920x1080@30fps RGB24 stream, following the classic CSourceStream
// push-source pattern (validated end-to-end via a throwaway spike before
// this was written).
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

    std::mutex frameMutex_;
    std::shared_ptr<FrameBuffer> currentFrame_;
    std::wstring lastLoadedPath_;

    LONGLONG frameIndex_ = 0;
    static constexpr REFERENCE_TIME kFrameDuration = 333333; // 100ns units, ~30fps
};

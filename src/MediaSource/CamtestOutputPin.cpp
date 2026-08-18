#include "CamtestOutputPin.h"
#include "../Imaging/ImageLoader.h"
#include "../Imaging/Letterbox.h"
#include "../Imaging/Rgb24Convert.h"
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>
#include <algorithm>
#include <string>

namespace {
std::wstring PipeNameForSlot(int slot) {
    return L"\\\\.\\pipe\\camtest_cam" + std::to_wstring(slot);
}
}

CamtestOutputPin::CamtestOutputPin(int slotIndex, HRESULT* phr, CSource* pFilter)
    : CSourceStream(L"CamtestOutputPin", phr, pFilter, L"Out"), slotIndex_(slotIndex) {
    currentFrame_ = FrameBuffer::MakeBlack();

    pipeClient_ = std::make_unique<NamedPipeClient>(
        PipeNameForSlot(slotIndex_),
        [this](const std::wstring& path) { OnImagePathReceived(path); });
    pipeClient_->Start();
}

CamtestOutputPin::~CamtestOutputPin() {
    pipeClient_->Stop();
}

STDMETHODIMP CamtestOutputPin::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IKsPropertySet) {
        return GetInterface(static_cast<IKsPropertySet*>(this), ppv);
    }
    if (riid == IID_IAMStreamConfig) {
        return GetInterface(static_cast<IAMStreamConfig*>(this), ppv);
    }
    return CSourceStream::NonDelegatingQueryInterface(riid, ppv);
}

void CamtestOutputPin::OnImagePathReceived(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (path == lastLoadedPath_) return; // heartbeat re-send of an already-loaded path
    }

    std::shared_ptr<FrameBuffer> newFrame;
    if (path.empty()) {
        newFrame = FrameBuffer::MakeBlack();
    } else {
        std::vector<uint8_t> pixels;
        UINT w = 0, h = 0;
        if (ImageLoader::LoadBGRA32(path, pixels, w, h)) {
            std::vector<uint8_t> canvas;
            Letterbox::ComposeCanvas(pixels, w, h, canvas);
            newFrame = Rgb24Convert::FromBGRA32Canvas(canvas);
        } else {
            newFrame = FrameBuffer::MakeBlack();
        }
    }

    std::lock_guard<std::mutex> lock(frameMutex_);
    currentFrame_ = newFrame;
    lastLoadedPath_ = path;
}

HRESULT CamtestOutputPin::FillBuffer(IMediaSample* pSample) {
    std::shared_ptr<FrameBuffer> frame;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        frame = currentFrame_;
    }

    BYTE* pData = nullptr;
    pSample->GetPointer(&pData);
    long size = pSample->GetSize();
    long copySize = (long)std::min((size_t)size, frame->bgr24.size());
    memcpy(pData, frame->bgr24.data(), copySize);
    pSample->SetActualDataLength(size);

    REFERENCE_TIME start = frameIndex_ * kFrameDuration;
    REFERENCE_TIME end = start + kFrameDuration;
    pSample->SetTime(&start, &end);
    pSample->SetSyncPoint(TRUE);
    ++frameIndex_;

    Sleep(33); // paces delivery to ~30fps; validated via the DirectShow spike
    return S_OK;
}

HRESULT CamtestOutputPin::GetMediaType(CMediaType* pmt) {
    VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
    if (!vih) return E_OUTOFMEMORY;
    ZeroMemory(vih, sizeof(VIDEOINFOHEADER));
    vih->AvgTimePerFrame = kFrameDuration;
    vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth = FrameBuffer::kWidth;
    vih->bmiHeader.biHeight = FrameBuffer::kHeight; // positive = bottom-up, matches FrameBuffer layout
    vih->bmiHeader.biPlanes = 1;
    vih->bmiHeader.biBitCount = 24;
    vih->bmiHeader.biCompression = BI_RGB;
    vih->bmiHeader.biSizeImage = (DWORD)FrameBuffer::kTotalSize;

    pmt->SetType(&MEDIATYPE_Video);
    pmt->SetSubtype(&MEDIASUBTYPE_RGB24);
    pmt->SetFormatType(&FORMAT_VideoInfo);
    pmt->SetTemporalCompression(FALSE);
    pmt->SetSampleSize((ULONG)FrameBuffer::kTotalSize);
    return S_OK;
}

HRESULT CamtestOutputPin::CheckMediaType(const CMediaType* pmt) {
    if (*pmt->Type() != MEDIATYPE_Video) return S_FALSE;
    if (*pmt->Subtype() != MEDIASUBTYPE_RGB24) return S_FALSE;
    if (*pmt->FormatType() != FORMAT_VideoInfo) return S_FALSE;
    VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)pmt->Format();
    if (!vih) return S_FALSE;
    if (vih->bmiHeader.biWidth != (LONG)FrameBuffer::kWidth) return S_FALSE;
    if (abs(vih->bmiHeader.biHeight) != (LONG)FrameBuffer::kHeight) return S_FALSE;
    if (vih->bmiHeader.biBitCount != 24) return S_FALSE;
    return S_OK;
}

HRESULT CamtestOutputPin::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) {
    pProps->cBuffers = 2;
    pProps->cbBuffer = (long)FrameBuffer::kTotalSize;
    ALLOCATOR_PROPERTIES actual;
    HRESULT hr = pAlloc->SetProperties(pProps, &actual);
    if (FAILED(hr)) return hr;
    if (actual.cbBuffer < pProps->cbBuffer) return E_FAIL;
    return S_OK;
}

// ---- IKsPropertySet: report PIN_CATEGORY_CAPTURE so consumers using
// ICaptureGraphBuilder2::RenderStream (the common real-world pattern) can
// find this pin. ----
HRESULT STDMETHODCALLTYPE CamtestOutputPin::Set(REFGUID, ULONG, LPVOID, ULONG, LPVOID, ULONG) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CamtestOutputPin::Get(REFGUID PropSet, ULONG Id, LPVOID, ULONG,
                                                  LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned) {
    if (PropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (Id != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    if (DataLength < sizeof(GUID)) return E_UNEXPECTED;
    *(GUID*)PropertyData = PIN_CATEGORY_CAPTURE;
    *BytesReturned = sizeof(GUID);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CamtestOutputPin::QuerySupported(REFGUID PropSet, ULONG Id, ULONG* pTypeSupport) {
    if (PropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (Id != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
    return S_OK;
}

// ---- IAMStreamConfig: single fixed format ----
HRESULT STDMETHODCALLTYPE CamtestOutputPin::SetFormat(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    CMediaType cmt(*pmt);
    if (CheckMediaType(&cmt) != S_OK) return E_FAIL;
    return S_OK; // only one format is ever actually possible; nothing to change
}

HRESULT STDMETHODCALLTYPE CamtestOutputPin::GetFormat(AM_MEDIA_TYPE** ppmt) {
    CMediaType cmt;
    HRESULT hr = GetMediaType(&cmt);
    if (FAILED(hr)) return hr;
    *ppmt = CreateMediaType(&cmt);
    return *ppmt ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE CamtestOutputPin::GetNumberOfCapabilities(int* piCount, int* piSize) {
    *piCount = 1;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CamtestOutputPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    if (iIndex != 0) return S_FALSE;
    HRESULT hr = GetFormat(ppmt);
    if (FAILED(hr)) return hr;

    VIDEO_STREAM_CONFIG_CAPS caps;
    ZeroMemory(&caps, sizeof(caps));
    caps.guid = FORMAT_VideoInfo;
    caps.VideoStandard = AnalogVideo_None;
    caps.InputSize.cx = FrameBuffer::kWidth;
    caps.InputSize.cy = FrameBuffer::kHeight;
    caps.MinCroppingSize = caps.MaxCroppingSize = caps.InputSize;
    caps.CropGranularityX = caps.CropGranularityY = 1;
    caps.CropAlignX = caps.CropAlignY = 0;
    caps.MinOutputSize = caps.MaxOutputSize = caps.InputSize;
    caps.OutputGranularityX = caps.OutputGranularityY = 1;
    caps.StretchTapsX = caps.StretchTapsY = 0;
    caps.ShrinkTapsX = caps.ShrinkTapsY = 0;
    caps.MinFrameInterval = caps.MaxFrameInterval = kFrameDuration;
    caps.MinBitsPerSecond = caps.MaxBitsPerSecond = (LONG)(FrameBuffer::kTotalSize * 8 * 30);
    memcpy(pSCC, &caps, sizeof(caps));
    return S_OK;
}

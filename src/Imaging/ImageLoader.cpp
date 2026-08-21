#include "ImageLoader.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace {

bool ConvertFrameToBGRA32(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame,
                           std::vector<uint8_t>& outPixels, UINT& outW, UINT& outH) {
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;
    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    UINT w = 0, h = 0;
    hr = converter->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0) return false;

    std::vector<uint8_t> pixels(size_t(w) * h * 4);
    UINT stride = w * 4;
    hr = converter->CopyPixels(nullptr, stride, (UINT)pixels.size(), pixels.data());
    if (FAILED(hr)) return false;

    outPixels = std::move(pixels);
    outW = w;
    outH = h;
    return true;
}

// Reads a small unsigned metadata value (GIF's Left/Top/Width/Height/Delay
// fields are all 16-bit per spec, but WIC has been seen to hand back
// VT_UI1/VT_UI2/VT_UI4 depending on the codec, so accept any of them).
UINT ReadMetaUInt(IWICMetadataQueryReader* reader, const wchar_t* name, UINT defaultValue) {
    if (!reader) return defaultValue;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    UINT result = defaultValue;
    if (SUCCEEDED(reader->GetMetadataByName(name, &pv))) {
        switch (pv.vt) {
        case VT_UI1: result = pv.bVal; break;
        case VT_UI2: result = pv.uiVal; break;
        case VT_UI4: result = pv.ulVal; break;
        default: break;
        }
    }
    PropVariantClear(&pv);
    return result;
}

constexpr UINT kMaxAnimatedFrames = 500;
constexpr DWORD kMinFrameDelayMs = 20;

} // namespace

namespace ImageLoader {

bool LoadBGRA32(const std::wstring& path, std::vector<uint8_t>& outPixels, UINT& outW, UINT& outH) {
    // COM may already be initialized (STA or MTA) on this thread by the
    // caller; CoInitializeEx returns S_FALSE in that case, which is fine.
    HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool weInitializedCom = SUCCEEDED(coInit) && coInit != S_FALSE;

    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) {
            ComPtr<IWICBitmapDecoder> decoder;
            hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
            if (SUCCEEDED(hr)) {
                ComPtr<IWICBitmapFrameDecode> frame;
                hr = decoder->GetFrame(0, &frame);
                if (SUCCEEDED(hr)) {
                    ok = ConvertFrameToBGRA32(factory.Get(), frame.Get(), outPixels, outW, outH);
                }
            }
        }
    } // all ComPtrs released before CoUninitialize

    if (weInitializedCom) {
        CoUninitialize();
    }

    return ok;
}

bool LoadAnimatedFrames(const std::wstring& path, std::vector<DecodedFrame>& outFrames,
                         UINT& outCanvasW, UINT& outCanvasH) {
    HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool weInitializedCom = SUCCEEDED(coInit) && coInit != S_FALSE;

    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) {
            ComPtr<IWICBitmapDecoder> decoder;
            hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
            if (SUCCEEDED(hr)) {
                UINT frameCount = 0;
                decoder->GetFrameCount(&frameCount);

                if (frameCount <= 1) {
                    // Non-animated (or single-frame GIF): identical to LoadBGRA32.
                    ComPtr<IWICBitmapFrameDecode> frame;
                    if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                        std::vector<uint8_t> pixels;
                        UINT w = 0, h = 0;
                        if (ConvertFrameToBGRA32(factory.Get(), frame.Get(), pixels, w, h)) {
                            outCanvasW = w;
                            outCanvasH = h;
                            outFrames.push_back({std::move(pixels), 100});
                            ok = true;
                        }
                    }
                } else {
                    ComPtr<IWICMetadataQueryReader> containerMeta;
                    decoder->GetMetadataQueryReader(&containerMeta);

                    ComPtr<IWICBitmapFrameDecode> firstFrame;
                    UINT firstFrameW = 0, firstFrameH = 0;
                    if (SUCCEEDED(decoder->GetFrame(0, &firstFrame))) {
                        firstFrame->GetSize(&firstFrameW, &firstFrameH);
                    }

                    UINT canvasW = ReadMetaUInt(containerMeta.Get(), L"/logscrdesc/Width", firstFrameW);
                    UINT canvasH = ReadMetaUInt(containerMeta.Get(), L"/logscrdesc/Height", firstFrameH);

                    if (canvasW > 0 && canvasH > 0) {
                        std::vector<uint8_t> accumulator(size_t(canvasW) * canvasH * 4, 0);
                        for (size_t i = 3; i < accumulator.size(); i += 4) accumulator[i] = 0xFF; // opaque black
                        const size_t accStride = size_t(canvasW) * 4;

                        UINT framesToProcess = std::min(frameCount, kMaxAnimatedFrames);
                        for (UINT i = 0; i < framesToProcess; ++i) {
                            ComPtr<IWICBitmapFrameDecode> frame;
                            if (FAILED(decoder->GetFrame(i, &frame))) break;

                            std::vector<uint8_t> frameBGRA;
                            UINT fw = 0, fh = 0;
                            if (!ConvertFrameToBGRA32(factory.Get(), frame.Get(), frameBGRA, fw, fh)) break;

                            ComPtr<IWICMetadataQueryReader> frameMeta;
                            frame->GetMetadataQueryReader(&frameMeta);
                            UINT left = ReadMetaUInt(frameMeta.Get(), L"/imgdesc/Left", 0);
                            UINT top = ReadMetaUInt(frameMeta.Get(), L"/imgdesc/Top", 0);
                            UINT rawDelayCenti = ReadMetaUInt(frameMeta.Get(), L"/grctlext/Delay", 0);

                            // Composite this frame's opaque pixels onto the accumulator
                            // at its declared offset; transparent pixels leave whatever
                            // was already there (the common "do not dispose" behavior).
                            UINT copyW = std::min(fw, (left < canvasW) ? canvasW - left : 0);
                            UINT copyH = std::min(fh, (top < canvasH) ? canvasH - top : 0);
                            const size_t frameStride = size_t(fw) * 4;
                            for (UINT y = 0; y < copyH; ++y) {
                                const uint8_t* srcRow = frameBGRA.data() + size_t(y) * frameStride;
                                uint8_t* dstRow = accumulator.data() + size_t(top + y) * accStride + size_t(left) * 4;
                                for (UINT x = 0; x < copyW; ++x) {
                                    const uint8_t* srcPx = srcRow + size_t(x) * 4;
                                    if (srcPx[3] != 0) { // opaque (or partially so) -- replace
                                        uint8_t* dstPx = dstRow + size_t(x) * 4;
                                        dstPx[0] = srcPx[0];
                                        dstPx[1] = srcPx[1];
                                        dstPx[2] = srcPx[2];
                                        dstPx[3] = 0xFF;
                                    }
                                }
                            }

                            DWORD delayMs = (rawDelayCenti == 0) ? 100 : rawDelayCenti * 10;
                            if (delayMs < kMinFrameDelayMs) delayMs = kMinFrameDelayMs;

                            outFrames.push_back({accumulator, delayMs}); // snapshot copy
                        }

                        if (!outFrames.empty()) {
                            outCanvasW = canvasW;
                            outCanvasH = canvasH;
                            ok = true;
                        }
                    }
                }
            }
        }
    }

    if (weInitializedCom) {
        CoUninitialize();
    }

    return ok;
}

} // namespace ImageLoader

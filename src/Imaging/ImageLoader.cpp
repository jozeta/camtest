#include "ImageLoader.h"
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

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
                    ComPtr<IWICFormatConverter> converter;
                    hr = factory->CreateFormatConverter(&converter);
                    if (SUCCEEDED(hr)) {
                        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                                     WICBitmapPaletteTypeCustom);
                        if (SUCCEEDED(hr)) {
                            UINT w = 0, h = 0;
                            hr = converter->GetSize(&w, &h);
                            if (SUCCEEDED(hr) && w > 0 && h > 0) {
                                std::vector<uint8_t> pixels(size_t(w) * h * 4);
                                UINT stride = w * 4;
                                hr = converter->CopyPixels(nullptr, stride, (UINT)pixels.size(), pixels.data());
                                if (SUCCEEDED(hr)) {
                                    outPixels = std::move(pixels);
                                    outW = w;
                                    outH = h;
                                    ok = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    } // all ComPtrs released before CoUninitialize

    if (weInitializedCom) {
        CoUninitialize();
    }

    return ok;
}

} // namespace ImageLoader

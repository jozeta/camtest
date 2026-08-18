#include "Letterbox.h"
#include "FrameBuffer.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace Letterbox {

bool ComposeCanvas(const std::vector<uint8_t>& srcBGRA, UINT srcW, UINT srcH,
                    std::vector<uint8_t>& outCanvasBGRA) {
    const UINT canvasW = FrameBuffer::kWidth;
    const UINT canvasH = FrameBuffer::kHeight;
    const UINT canvasStride = canvasW * 4;

    outCanvasBGRA.assign(size_t(canvasStride) * canvasH, 0);
    // Fill opaque black (B=0,G=0,R=0,A=255) for every pixel.
    for (size_t i = 3; i < outCanvasBGRA.size(); i += 4) {
        outCanvasBGRA[i] = 0xFF;
    }

    if (srcW == 0 || srcH == 0 || srcBGRA.size() < size_t(srcW) * srcH * 4) {
        return false;
    }

    HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool weInitializedCom = SUCCEEDED(coInit) && coInit != S_FALSE;

    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) {
            ComPtr<IWICBitmap> srcBitmap;
            // CreateBitmapFromMemory copies the pixel data in, so srcBGRA
            // doesn't need to outlive this call.
            hr = factory->CreateBitmapFromMemory(srcW, srcH, GUID_WICPixelFormat32bppBGRA,
                                                   srcW * 4, (UINT)srcBGRA.size(),
                                                   const_cast<BYTE*>(srcBGRA.data()), &srcBitmap);
            if (SUCCEEDED(hr)) {
                double scale = std::min(double(canvasW) / srcW, double(canvasH) / srcH);
                UINT destW = std::max(1u, (UINT)std::lround(srcW * scale));
                UINT destH = std::max(1u, (UINT)std::lround(srcH * scale));
                destW = std::min(destW, canvasW);
                destH = std::min(destH, canvasH);

                if (destW == srcW && destH == srcH) {
                    // No scaling needed; copy the source directly.
                    UINT offsetX = (canvasW - destW) / 2;
                    UINT offsetY = (canvasH - destH) / 2;
                    BYTE* dst = outCanvasBGRA.data() + size_t(offsetY) * canvasStride + size_t(offsetX) * 4;
                    size_t bytesNeeded = size_t(canvasStride) * (destH - 1) + size_t(destW) * 4;
                    hr = srcBitmap->CopyPixels(nullptr, canvasStride, (UINT)bytesNeeded, dst);
                    ok = SUCCEEDED(hr);
                } else {
                    ComPtr<IWICBitmapScaler> scaler;
                    hr = factory->CreateBitmapScaler(&scaler);
                    if (SUCCEEDED(hr)) {
                        hr = scaler->Initialize(srcBitmap.Get(), destW, destH,
                                                  WICBitmapInterpolationModeFant);
                        if (SUCCEEDED(hr)) {
                            UINT offsetX = (canvasW - destW) / 2;
                            UINT offsetY = (canvasH - destH) / 2;
                            BYTE* dst = outCanvasBGRA.data() + size_t(offsetY) * canvasStride + size_t(offsetX) * 4;
                            size_t bytesNeeded = size_t(canvasStride) * (destH - 1) + size_t(destW) * 4;
                            hr = scaler->CopyPixels(nullptr, canvasStride, (UINT)bytesNeeded, dst);
                            ok = SUCCEEDED(hr);
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

} // namespace Letterbox

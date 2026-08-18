#include "Rgb24Convert.h"

namespace Rgb24Convert {

std::shared_ptr<FrameBuffer> FromBGRA32Canvas(const std::vector<uint8_t>& canvasBGRA) {
    auto fb = std::make_shared<FrameBuffer>();
    const UINT32 w = FrameBuffer::kWidth;
    const UINT32 h = FrameBuffer::kHeight;
    const size_t srcStride = size_t(w) * 4;

    if (canvasBGRA.size() < srcStride * h) {
        return fb; // malformed input: leave as black (default-constructed... actually zero, close enough)
    }

    for (UINT32 y = 0; y < h; ++y) {
        const uint8_t* srcRow = canvasBGRA.data() + size_t(y) * srcStride;
        // Row y of the (top-down) source maps to row (h-1-y) of the
        // bottom-up destination.
        uint8_t* dstRow = fb->bgr24.data() + size_t(h - 1 - y) * FrameBuffer::kStride;
        for (UINT32 x = 0; x < w; ++x) {
            const uint8_t* srcPx = srcRow + size_t(x) * 4;
            uint8_t* dstPx = dstRow + size_t(x) * 3;
            dstPx[0] = srcPx[0]; // B
            dstPx[1] = srcPx[1]; // G
            dstPx[2] = srcPx[2]; // R
        }
    }

    return fb;
}

} // namespace Rgb24Convert

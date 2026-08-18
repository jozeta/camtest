#pragma once
#include <windows.h>
#include <vector>
#include <memory>
#include <cstdint>

// Immutable RGB24 frame, always exactly kWidth x kHeight, stored BOTTOM-UP
// (row 0 = bottom scanline, 3 bytes/pixel in B,G,R order) -- the
// conventional orientation DirectShow capture consumers expect for
// MEDIASUBTYPE_RGB24 with a positive biHeight, and the byte order GDI/DIB
// has always used. Storing it pre-flipped here means the per-frame
// FillBuffer copy is a plain memcpy with no per-frame flip cost.
//
// Producing a new instance (on image load / live-swap) never mutates an
// existing one, so a pump thread that grabbed a shared_ptr to the
// "current" buffer can keep reading it safely even if a swap happens
// concurrently on another thread.
struct FrameBuffer {
    static constexpr UINT32 kWidth = 1920;
    static constexpr UINT32 kHeight = 1080;
    static constexpr size_t kBytesPerPixel = 3;
    static constexpr size_t kStride = size_t(kWidth) * kBytesPerPixel;
    static constexpr size_t kTotalSize = kStride * kHeight;

    std::vector<uint8_t> bgr24; // size == kTotalSize, bottom-up

    FrameBuffer() : bgr24(kTotalSize) {}

    // A buffer filled entirely with black.
    static std::shared_ptr<FrameBuffer> MakeBlack() {
        auto fb = std::make_shared<FrameBuffer>();
        std::fill(fb->bgr24.begin(), fb->bgr24.end(), uint8_t(0));
        return fb;
    }
};

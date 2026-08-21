#pragma once
#include <windows.h>
#include <vector>
#include <memory>
#include <cstdint>

// Immutable RGB24 frame, always exactly width x height (per-instance, set
// at construction -- see VCamSlots/registry-based per-slot resolution),
// stored BOTTOM-UP (row 0 = bottom scanline, 3 bytes/pixel in B,G,R order)
// -- the conventional orientation DirectShow capture consumers expect for
// MEDIASUBTYPE_RGB24 with a positive biHeight, and the byte order GDI/DIB
// has always used. Storing it pre-flipped here means the per-frame
// FillBuffer copy is a plain memcpy with no per-frame flip cost.
//
// Producing a new instance (on image load / live-swap) never mutates an
// existing one, so a pump thread that grabbed a shared_ptr to the
// "current" buffer can keep reading it safely even if a swap happens
// concurrently on another thread.
struct FrameBuffer {
    static constexpr size_t kBytesPerPixel = 3;
    static constexpr UINT32 kDefaultWidth = 1920;
    static constexpr UINT32 kDefaultHeight = 1080;

    UINT32 width;
    UINT32 height;
    std::vector<uint8_t> bgr24; // size == Stride()*height, bottom-up

    explicit FrameBuffer(UINT32 w = kDefaultWidth, UINT32 h = kDefaultHeight)
        : width(w), height(h), bgr24(size_t(w) * kBytesPerPixel * h) {}

    size_t Stride() const { return size_t(width) * kBytesPerPixel; }
    size_t TotalSize() const { return Stride() * height; }

    // A buffer filled entirely with black.
    static std::shared_ptr<FrameBuffer> MakeBlack(UINT32 w = kDefaultWidth, UINT32 h = kDefaultHeight) {
        auto fb = std::make_shared<FrameBuffer>(w, h);
        std::fill(fb->bgr24.begin(), fb->bgr24.end(), uint8_t(0));
        return fb;
    }
};

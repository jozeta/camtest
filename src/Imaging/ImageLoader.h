#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>
#include <string>

namespace ImageLoader {

// Decodes any WIC-supported PNG/JPG at `path` into a top-down 32bpp BGRA
// buffer (stride = width*4). Returns false on failure (bad path, corrupt
// file, unsupported codec) and leaves outPixels/outW/outH untouched.
// Must be called on a thread that has CoInitializeEx'd (this function will
// initialize COM for the calling thread on demand if it hasn't been already,
// and will not uninitialize an apartment it did not create).
bool LoadBGRA32(const std::wstring& path, std::vector<uint8_t>& outPixels, UINT& outW, UINT& outH);

struct DecodedFrame {
    std::vector<uint8_t> bgra; // top-down 32bpp BGRA, full canvas size, pre-composited
    DWORD delayMs;
};

// Decodes every frame of an animated image (GIF) into a sequence of full,
// pre-composited top-down BGRA canvases at the image's logical screen size
// (outCanvasW x outCanvasH), each with its display duration. For a
// non-animated image, or a GIF with exactly one frame, this returns a
// single frame equivalent to LoadBGRA32.
//
// Compositing follows the common "do not dispose" GIF convention: each
// frame's opaque pixels overwrite an accumulator canvas at that frame's
// declared offset, transparent pixels leave the accumulator untouched.
// That covers the vast majority of real-world GIFs, but does not
// special-case the rarer "restore to background"/"restore to previous"
// disposal methods (falls back to the same overwrite behavior, which can
// show minor ghosting on GIFs that rely on them). Loop-count metadata is
// ignored -- callers always loop the returned sequence indefinitely.
// Capped at 500 frames to bound memory. Same threading/COM contract as
// LoadBGRA32.
bool LoadAnimatedFrames(const std::wstring& path, std::vector<DecodedFrame>& outFrames,
                         UINT& outCanvasW, UINT& outCanvasH);

} // namespace ImageLoader

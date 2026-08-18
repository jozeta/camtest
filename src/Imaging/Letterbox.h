#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>

namespace Letterbox {

// Composes a source 32bpp BGRA image of size (srcW x srcH) into a
// 1920x1080 32bpp BGRA canvas: uniformly scaled to fit entirely inside the
// canvas (preserving aspect ratio, never cropping), centered, with the
// surrounding area filled opaque black. Output is always exactly
// 1920*1080*4 bytes, top-down, stride 1920*4.
// Returns false on a WIC failure (canvas is left black either way).
bool ComposeCanvas(const std::vector<uint8_t>& srcBGRA, UINT srcW, UINT srcH,
                    std::vector<uint8_t>& outCanvasBGRA);

} // namespace Letterbox

#pragma once
#include <windows.h>
#include <vector>
#include <cstdint>

namespace Letterbox {

// Composes a source 32bpp BGRA image of size (srcW x srcH) into a
// (canvasW x canvasH) 32bpp BGRA canvas: uniformly scaled to fit entirely
// inside the canvas (preserving aspect ratio, never cropping), centered,
// with the surrounding area filled opaque black. Output is always exactly
// canvasW*canvasH*4 bytes, top-down, stride canvasW*4.
// Returns false on a WIC failure (canvas is left black either way).
bool ComposeCanvas(const std::vector<uint8_t>& srcBGRA, UINT srcW, UINT srcH,
                    UINT canvasW, UINT canvasH, std::vector<uint8_t>& outCanvasBGRA);

} // namespace Letterbox

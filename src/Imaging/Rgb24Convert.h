#pragma once
#include "FrameBuffer.h"
#include <vector>
#include <memory>

namespace Rgb24Convert {

// Converts a 1920x1080 32bpp BGRA, TOP-DOWN canvas (as produced by
// Letterbox::ComposeCanvas) into a new immutable FrameBuffer: drops the
// alpha byte and flips row order to bottom-up, matching what
// MEDIASUBTYPE_RGB24 with a positive biHeight expects.
std::shared_ptr<FrameBuffer> FromBGRA32Canvas(const std::vector<uint8_t>& canvasBGRA);

} // namespace Rgb24Convert

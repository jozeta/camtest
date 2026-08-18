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

} // namespace ImageLoader

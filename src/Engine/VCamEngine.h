#pragma once
#include <windows.h>
#include <string>
#include <memory>
#include <array>
#include "../Ipc/NamedPipeServer.h"
#include "../VCamSlots.h"

// Owns the per-slot named pipe servers and the (de)registration flow.
// Registration is a one-time, elevated, standing operation (COM CLSID +
// DirectShow filter-category registration); "Start"/"Stop" per slot only
// control whether that slot's pipe server is actively serving frames --
// the device itself remains enumerable to consumers regardless (showing
// black until Start is called for that slot).
class VCamEngine {
public:
    VCamEngine();
    ~VCamEngine();

    // Registration (call from an elevated process -- see main.cpp's
    // -RegServer/-UnregServer modes). Returns false with lastError set on
    // any failure; a partial success can happen if a later slot fails and
    // is not automatically rolled back (matches the app's general
    // no-admin-after-first-time, no-crash philosophy).
    static bool RegisterAllSlots(std::wstring& outError);
    static bool UnregisterAllSlots(std::wstring& outError);

    bool StartSlot(int slot, const std::wstring& imagePath);
    void StopSlot(int slot);
    bool IsSlotRunning(int slot) const { return servers_[slot] != nullptr; }

    // Live image swap: no-op if the slot isn't currently running.
    void SetImagePath(int slot, const std::wstring& imagePath);

private:
    std::array<std::unique_ptr<NamedPipeServer>, VCamSlots::kMaxSlots> servers_;
};

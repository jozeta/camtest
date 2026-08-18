#pragma once
#include <windows.h>
#include <string>

// Fixed, compile-time CLSIDs -- one per camera slot (0-3). Each is
// separately registered as its own DirectShow capture filter/device, and
// each slot has its own fixed-name pipe (see NamedPipeServer/Client).
//
// These are private to this app (generated once, never reused elsewhere).
// Declared extern here (no storage) -- defined once in VCamSlots.cpp via
// initguid.h, to avoid multiply-defined-symbol link errors from being
// included in several translation units.
extern const CLSID CLSID_CamtestFilterSlot0;
extern const CLSID CLSID_CamtestFilterSlot1;
extern const CLSID CLSID_CamtestFilterSlot2;
extern const CLSID CLSID_CamtestFilterSlot3;

namespace VCamSlots {

constexpr int kMaxSlots = 4;

inline const CLSID& ClsidForSlot(int slot) {
    static const CLSID* kClsids[kMaxSlots] = {
        &CLSID_CamtestFilterSlot0, &CLSID_CamtestFilterSlot1,
        &CLSID_CamtestFilterSlot2, &CLSID_CamtestFilterSlot3,
    };
    return *kClsids[slot];
}

// Returns the slot index (0-3) for a CLSID, or -1 if it doesn't match any slot.
inline int SlotForClsid(REFCLSID clsid) {
    for (int i = 0; i < kMaxSlots; ++i) {
        if (IsEqualCLSID(clsid, ClsidForSlot(i))) return i;
    }
    return -1;
}

inline std::wstring FriendlyNameForSlot(int slot) {
    return L"camtest Virtual Camera " + std::to_wstring(slot + 1);
}

} // namespace VCamSlots

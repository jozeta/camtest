#pragma once
#include <streams.h>
#include "CamtestOutputPin.h"

// One filter instance per active connection to a camera slot's CLSID.
// CSource already implements IBaseFilter/IPersist/pin enumeration; we just
// own the one CamtestOutputPin.
class CamtestSourceFilter : public CSource {
public:
    static CUnknown* WINAPI CreateInstance(int slotIndex, LPUNKNOWN pUnk, HRESULT* phr);

private:
    CamtestSourceFilter(int slotIndex, LPUNKNOWN pUnk, HRESULT* phr);

    CamtestOutputPin* pin_ = nullptr; // lifetime owned by CSource's pin list
};

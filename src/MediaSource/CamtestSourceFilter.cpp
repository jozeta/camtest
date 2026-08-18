#include "CamtestSourceFilter.h"
#include "../VCamSlots.h"

CUnknown* WINAPI CamtestSourceFilter::CreateInstance(int slotIndex, LPUNKNOWN pUnk, HRESULT* phr) {
    auto* filter = new CamtestSourceFilter(slotIndex, pUnk, phr);
    if (phr && FAILED(*phr)) {
        delete filter;
        return nullptr;
    }
    return filter;
}

CamtestSourceFilter::CamtestSourceFilter(int slotIndex, LPUNKNOWN pUnk, HRESULT* phr)
    : CSource(L"CamtestSourceFilter", pUnk, VCamSlots::ClsidForSlot(slotIndex)) {
    pin_ = new CamtestOutputPin(slotIndex, phr, this);
    if (phr) *phr = pin_ ? S_OK : E_OUTOFMEMORY;
}

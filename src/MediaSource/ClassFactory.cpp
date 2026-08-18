#include "ClassFactory.h"
#include "CamtestSourceFilter.h"
#include "../VCamSlots.h"
#include <streams.h> // for CUnknown, NonDelegating*
#include <atomic>

namespace {
std::atomic<LONG> g_lockCount{0};
}

ClassFactory::ClassFactory(int slotIndex) : slotIndex_(slotIndex) {}

HRESULT STDMETHODCALLTYPE ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ClassFactory::AddRef() {
    return InterlockedIncrement(&refs_);
}

ULONG STDMETHODCALLTYPE ClassFactory::Release() {
    LONG r = InterlockedDecrement(&refs_);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    *ppv = nullptr;
    HRESULT hr = S_OK;
    CUnknown* punk = CamtestSourceFilter::CreateInstance(slotIndex_, pUnkOuter, &hr);
    if (FAILED(hr) || !punk) {
        return FAILED(hr) ? hr : E_FAIL;
    }
    punk->NonDelegatingAddRef();
    hr = punk->NonDelegatingQueryInterface(riid, ppv);
    punk->NonDelegatingRelease();
    return hr;
}

HRESULT STDMETHODCALLTYPE ClassFactory::LockServer(BOOL fLock) {
    if (fLock) ++g_lockCount;
    else --g_lockCount;
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    int slot = VCamSlots::SlotForClsid(rclsid);
    if (slot < 0) return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new ClassFactory(slot);
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return g_lockCount.load() == 0 ? S_OK : S_FALSE;
}

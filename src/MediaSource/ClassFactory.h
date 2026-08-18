#pragma once
#include <windows.h>
#include <unknwn.h>

// Our own minimal IClassFactory -- deliberately not using BaseClasses'
// CFactoryTemplate/g_Templates mechanism (which assumes it owns
// DllGetClassObject/DllRegisterServer too) since we need multi-CLSID
// dispatch (one CLSID per camera slot) and our own registration flow.
class ClassFactory : public IClassFactory {
public:
    explicit ClassFactory(int slotIndex);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override;

private:
    LONG refs_ = 1;
    int slotIndex_;
};

// Exported COM server entry points (wired via camtest.def). Dispatch based
// on which of the 4 slot CLSIDs was requested.
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv);
STDAPI DllCanUnloadNow();

#pragma once

// CClassFactory —— 标准 COM 类厂，创建 CTextService 实例
#include "Globals.h"

class CClassFactory : public IClassFactory
{
public:
    CClassFactory() : _cRef(1) { DllAddRef(); }
    virtual ~CClassFactory() { DllRelease(); }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
    {
        if (ppvObj == nullptr) return E_INVALIDARG;
        *ppvObj = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
        {
            *ppvObj = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override  { return InterlockedIncrement(&_cRef); }
    STDMETHODIMP_(ULONG) Release() override
    {
        LONG c = InterlockedDecrement(&_cRef);
        if (c == 0) delete this;
        return c;
    }

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) override;
    STDMETHODIMP LockServer(BOOL fLock) override
    {
        if (fLock) DllAddRef(); else DllRelease();
        return S_OK;
    }

private:
    LONG _cRef;
};

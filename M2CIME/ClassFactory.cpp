// ClassFactory.cpp
#include "Globals.h"
#include "ClassFactory.h"
#include "TextService.h"

STDMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;

    CTextService* pService = new (std::nothrow) CTextService();
    if (pService == nullptr) return E_OUTOFMEMORY;

    HRESULT hr = pService->QueryInterface(riid, ppvObj);
    pService->Release();
    return hr;
}

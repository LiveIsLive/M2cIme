// dllmain.cpp —— M2CIME TSF 文本服务 COM 入口
#include "Globals.h"
#include "ClassFactory.h"

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = 0;

void DllAddRef()    { InterlockedIncrement(&g_cRefDll); }
void DllRelease()   { InterlockedDecrement(&g_cRefDll); }

// 注册/注销（Register.cpp）
BOOL RegisterServer();
void UnregisterServer();

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hInstance;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualCLSID(rclsid, CLSID_M2CTextService))
    {
        CClassFactory* pFactory = new (std::nothrow) CClassFactory();
        if (pFactory == nullptr) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppvObj);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow(void)
{
    return (g_cRefDll <= 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer(void)
{
    return RegisterServer() ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer(void)
{
    UnregisterServer();
    return S_OK;
}

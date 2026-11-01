// Register.cpp —— COM 注册表 + TSF Profile 注册/注销
#include "Globals.h"

#define M2C_CLSID_STR   L"{7E2A1F30-9C4B-4E1D-B5F2-3A6D8C0E1A42}"
#define M2C_PROFILE_STR L"{A3F5C8D1-2B6E-4A9F-8D3C-5E7B9A1C2D04}"

static BOOL WriteRegKey(HKEY hRoot, LPCWSTR subKey, LPCWSTR valueName, LPCWSTR data)
{
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(hRoot, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return FALSE;
    LSTATUS st = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                                (const BYTE*)data,
                                (DWORD)((wcslen(data) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
    return st == ERROR_SUCCESS;
}

static void DeleteRegTree(HKEY hRoot, LPCWSTR subKey)
{
    RegDeleteTreeW(hRoot, subKey);
}

// Profile 图标：优先用与 DLL 同目录的 M2CIME.ico。
// 32 位和 64 位 regsvr32 都会写同一个 Profile，若直接用自己的 DLL 路径，
// 后注册的一方会把 IconFile 覆盖成另一架构的 DLL，导致该架构的语言栏加载不到图标。
static void GetProfileIconFile(const WCHAR* dllPath, WCHAR* out, size_t cch)
{
    wcscpy_s(out, cch, dllPath);
    WCHAR* slash = wcsrchr(out, L'\\');
    if (slash == nullptr) return;

    *(slash + 1) = L'\0';
    if (wcscat_s(out, cch, L"M2CIME.ico") != 0)
    {
        wcscpy_s(out, cch, dllPath);
        return;
    }
    if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES)
        wcscpy_s(out, cch, dllPath);   // 没有独立图标文件时退回 DLL 资源
}

// 打开经典语言栏，让文本服务的语言栏按钮可见（Win10/11 默认隐藏）
static void EnableLanguageBar()
{
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\CTF\\LangBar", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return;

    DWORD show = 3;      // 3 = 显示语言栏
    RegSetValueExW(hKey, L"ShowStatus", 0, REG_DWORD, (const BYTE*)&show, sizeof(show));

    DWORD extra = 1;     // 显示额外输入法按钮
    RegSetValueExW(hKey, L"ExtraImeButton", 0, REG_DWORD, (const BYTE*)&extra, sizeof(extra));

    RegCloseKey(hKey);
}

// 把输入法挂进当前用户的输入法列表（Win+Space、任务栏指示器、设置里才能选中它）
static void InstallLayoutOrTipEntry(BOOL fInstall)
{
    const DWORD ILOT_UNINSTALL = 0x00000001;

    HMODULE hInput = LoadLibraryW(L"input.dll");
    if (!hInput) return;

    typedef BOOL (WINAPI *PFN_ILOT)(LPCWSTR, DWORD);
    PFN_ILOT pfn = (PFN_ILOT)GetProcAddress(hInput, "InstallLayoutOrTip");
    if (pfn)
    {
        WCHAR tip[128];
        swprintf_s(tip, L"0804:%s%s", M2C_CLSID_STR, M2C_PROFILE_STR);
        pfn(tip, fInstall ? 0 : ILOT_UNINSTALL);
    }
    FreeLibrary(hInput);
}

// 支持的语言：简体中文（中国大陆）
static const LANGID g_langid = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

BOOL RegisterServer()
{
    // 1. 取得 DLL 全路径
    WCHAR dllPath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(g_hInst, dllPath, ARRAYSIZE(dllPath)) == 0)
        return FALSE;

    // 2. 注册 COM CLSID
    WCHAR key[256];
    swprintf_s(key, L"CLSID\\%s", M2C_CLSID_STR);
    if (!WriteRegKey(HKEY_CLASSES_ROOT, key, nullptr, M2C_SERVICE_DESC)) return FALSE;

    swprintf_s(key, L"CLSID\\%s\\InprocServer32", M2C_CLSID_STR);
    if (!WriteRegKey(HKEY_CLASSES_ROOT, key, nullptr, dllPath)) return FALSE;
    if (!WriteRegKey(HKEY_CLASSES_ROOT, key, L"ThreadingModel", L"Apartment")) return FALSE;

    // 3. 注册 TSF 文本服务
    ITfInputProcessorProfiles* pProfiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfiles, (void**)&pProfiles);
    if (FAILED(hr)) return FALSE;

    // 3a. 注册文本服务
    hr = pProfiles->Register(CLSID_M2CTextService);
    if (FAILED(hr)) { pProfiles->Release(); return FALSE; }

    // 3b. 添加语言 profile（图标用独立 .ico，32/64 位都能加载）
    WCHAR iconFile[MAX_PATH] = { 0 };
    GetProfileIconFile(dllPath, iconFile, ARRAYSIZE(iconFile));

    hr = pProfiles->AddLanguageProfile(
        CLSID_M2CTextService,
        g_langid,
        GUID_M2CProfile,
        M2C_SERVICE_DESC,
        (ULONG)(wcslen(M2C_SERVICE_DESC)),
        iconFile,
        (ULONG)wcslen(iconFile),
        0);
    if (FAILED(hr)) { pProfiles->Release(); return FALSE; }

    // 3c. 默认启用
    pProfiles->EnableLanguageProfile(CLSID_M2CTextService, g_langid, GUID_M2CProfile, TRUE);

    // 3d. 注册为键盘类别（TIP keyboard）
    ITfCategoryMgr* pCat = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr, (void**)&pCat)))
    {
        pCat->RegisterCategory(CLSID_M2CTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_M2CTextService);
        pCat->RegisterCategory(CLSID_M2CTextService, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_M2CTextService);
        pCat->Release();
    }

    // 3e. 打开语言栏，保证模式切换按钮可见
    EnableLanguageBar();

    // 3f. 加入当前用户的输入法列表（否则 Win+Space / 任务栏指示器里选不到）
    InstallLayoutOrTipEntry(TRUE);

    pProfiles->Release();
    return TRUE;
}

void UnregisterServer()
{
    // 0. 从用户输入法列表中移除
    InstallLayoutOrTipEntry(FALSE);

    // 1. 注销 TSF
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles, (void**)&pProfiles)))
    {
        pProfiles->Unregister(CLSID_M2CTextService); // 会连带移除 profile
        pProfiles->Release();
    }

    // 2. 删除 COM 注册表
    WCHAR key[256];
    swprintf_s(key, L"CLSID\\%s", M2C_CLSID_STR);
    DeleteRegTree(HKEY_CLASSES_ROOT, key);
}

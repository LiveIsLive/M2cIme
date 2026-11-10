// LangBarSettingsButton.cpp —— 语言栏设置按钮（点击打开设置对话框）
#include "Globals.h"
#include "LangBarSettingsButton.h"
#include "TextService.h"
#include "resource.h"
#include <windowsx.h>   // ComboBox_* 宏

// 设置字符：齿轮（U+2699），作为"设置"入口的图标
// （原暂定字符 U+1202D 楔形文字因系统字体不支持无法显示，改用通用齿轮符号）
#define M2C_SETTINGS_GLYPH  L"\u2699"

// ===== 设置对话框 =====

static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        // lParam 由 DialogBoxParam 传入，保存 CTextService 指针
        CTextService* pService = reinterpret_cast<CTextService*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pService);

        HWND hCombo = GetDlgItem(hDlg, IDC_INPUT_MODE_COMBO);
        if (hCombo)
        {
            ComboBox_AddString(hCombo, L"全拼");
            ComboBox_AddString(hCombo, L"双拼");
            int sel = (pService && pService->GetPinyinMode() == IME_PINYIN_DOUBLE) ? 1 : 0;
            ComboBox_SetCurSel(hCombo, sel);
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDOK)
        {
            CTextService* pService =
                reinterpret_cast<CTextService*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            HWND hCombo = GetDlgItem(hDlg, IDC_INPUT_MODE_COMBO);
            int sel = hCombo ? ComboBox_GetCurSel(hCombo) : 0;
            if (pService)
                pService->SetPinyinMode((sel == 1) ? IME_PINYIN_DOUBLE : IME_PINYIN_FULL);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// ===== 设置对话框 =====

// 打开模态"设置"对话框。自绘状态条（StatusBar.cpp）与语言栏按钮共用此入口。
void M2CShowSettingsDialog(CTextService* pService, HWND hwndOwner)
{
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_SETTINGS),
                    hwndOwner, SettingsDlgProc, (LPARAM)pService);
}

// ===== CLangBarSettingsButton =====

CLangBarSettingsButton::CLangBarSettingsButton(CTextService* pService)
    : _cRef(1)
    , _pService(pService)
    , _pSink(nullptr)
    , _dwSinkCookie(TF_INVALID_COOKIE)
    , _hIcon(nullptr)
{
    DllAddRef();
}

CLangBarSettingsButton::~CLangBarSettingsButton()
{
    if (_hIcon) { DestroyIcon(_hIcon); _hIcon = nullptr; }
    DllRelease();
}

STDMETHODIMP CLangBarSettingsButton::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
    {
        *ppvObj = (ITfLangBarItemButton*)this;
    }
    else if (IsEqualIID(riid, IID_ITfSource))
    {
        *ppvObj = (ITfSource*)this;
    }
    else
    {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CLangBarSettingsButton::AddRef()  { return InterlockedIncrement(&_cRef); }
STDMETHODIMP_(ULONG) CLangBarSettingsButton::Release()
{
    LONG c = InterlockedDecrement(&_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CLangBarSettingsButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = CLSID_M2CTextService;
    pInfo->guidItem = GUID_M2CLangBarSettingsButton;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;  // 非菜单式，直接点击
    pInfo->ulSort = 4;  // 排在模式、全角/半角、标点、简/繁按钮之后
    wcscpy_s(pInfo->szDescription, M2C_SETTINGS_BTN_DESC);
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::Show(BOOL /*fShow*/) { return S_OK; }

STDMETHODIMP CLangBarSettingsButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (!pbstrToolTip) return E_INVALIDARG;
    *pbstrToolTip = SysAllocString(L"设置");
    return (*pbstrToolTip) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarSettingsButton::OnClick(TfLBIClick /*click*/, POINT /*pt*/, const RECT* /*prcArea*/)
{
    // 点击打开设置对话框（模态）
    if (_pService)
        M2CShowSettingsDialog(_pService, nullptr);
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::InitMenu(ITfMenu* /*pMenu*/) { return S_OK; }
STDMETHODIMP CLangBarSettingsButton::OnMenuSelect(UINT /*wID*/) { return S_OK; }

STDMETHODIMP CLangBarSettingsButton::GetIcon(HICON* phIcon)
{
    if (!phIcon) return E_INVALIDARG;
    *phIcon = _GetSettingsIcon();
    return (*phIcon) ? S_OK : E_FAIL;
}

HICON CLangBarSettingsButton::_GetSettingsIcon()
{
    if (_hIcon == nullptr)
        _hIcon = _CreateSettingsIcon(M2C_SETTINGS_GLYPH, RGB(90, 90, 90));
    return _hIcon;
}

// 用 GDI 画一个 16x16 的圆角小方块，中间是白色设置字符
HICON CLangBarSettingsButton::_CreateSettingsIcon(const WCHAR* text, COLORREF bg)
{
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = cx;
    bi.bV5Height      = -cy;
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return nullptr;

    void* bits = nullptr;
    HBITMAP hbmColor = CreateDIBSection(hdcScreen, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);

    BITMAPINFO maskBmi = {};
    maskBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    maskBmi.bmiHeader.biWidth       = cx;
    maskBmi.bmiHeader.biHeight      = -cy;
    maskBmi.bmiHeader.biPlanes      = 1;
    maskBmi.bmiHeader.biBitCount    = 1;
    maskBmi.bmiHeader.biCompression = BI_RGB;
    void* maskBits = nullptr;
    HBITMAP hbmMask = CreateDIBSection(hdcScreen, &maskBmi, DIB_RGB_COLORS, &maskBits, nullptr, 0);

    if (!hbmColor || !hbmMask || !bits || !maskBits)
    {
        if (hbmColor) DeleteObject(hbmColor);
        if (hbmMask)  DeleteObject(hbmMask);
        ReleaseDC(nullptr, hdcScreen);
        return nullptr;
    }

    ZeroMemory(bits, (size_t)cx * (size_t)cy * 4);
    ZeroMemory(maskBits, (size_t)(((cx + 31) / 32) * 4) * (size_t)cy);

    HDC memDC = CreateCompatibleDC(hdcScreen);
    HGDIOBJ oldBmp = SelectObject(memDC, hbmColor);

    HBRUSH brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, bg);
    HGDIOBJ oldBrush = SelectObject(memDC, brush);
    HGDIOBJ oldPen = SelectObject(memDC, pen);
    RoundRect(memDC, 0, 0, cx, cy, 5, 5);

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    HFONT font = CreateFontW(-MulDiv(9, GetDeviceCaps(hdcScreen, LOGPIXELSY), 72), 0, 0, 0,
                             FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(memDC, font);
    RECT rc = { 0, 0, cx, cy };
    DrawTextW(memDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    DWORD* px = (DWORD*)bits;
    for (int i = 0; i < cx * cy; ++i)
    {
        if ((px[i] & 0x00FFFFFF) != 0)
            px[i] |= 0xFF000000;
    }

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = hbmMask;
    ii.hbmColor = hbmColor;
    HICON hIcon = CreateIconIndirect(&ii);

    SelectObject(memDC, oldFont);
    SelectObject(memDC, oldBrush);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBmp);
    DeleteObject(font);
    DeleteObject(brush);
    DeleteObject(pen);
    DeleteDC(memDC);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    ReleaseDC(nullptr, hdcScreen);
    return hIcon;
}

STDMETHODIMP CLangBarSettingsButton::GetText(BSTR* pbstrText)
{
    if (!pbstrText) return E_INVALIDARG;
    *pbstrText = SysAllocString(M2C_SETTINGS_GLYPH);
    return (*pbstrText) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarSettingsButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (_pSink) return CONNECT_E_ADVISELIMIT;
    if (!punk || !pdwCookie) return E_INVALIDARG;

    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&_pSink)))
        return E_NOINTERFACE;

    _dwSinkCookie = 0x53455447; // 'SETG'
    *pdwCookie = _dwSinkCookie;
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != _dwSinkCookie || !_pSink)
        return CONNECT_E_NOCONNECTION;
    _pSink->Release();
    _pSink = nullptr;
    _dwSinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}

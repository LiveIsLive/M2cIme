// LangBarVariantButton.cpp —— 语言栏简体/繁体切换按钮
#include "Globals.h"
#include "LangBarVariantButton.h"
#include "TextService.h"

CLangBarVariantButton::CLangBarVariantButton(CTextService* pService)
    : _cRef(1)
    , _pService(pService)
    , _pSink(nullptr)
    , _dwSinkCookie(TF_INVALID_COOKIE)
    , _hIconSimplified(nullptr)
    , _hIconTraditional(nullptr)
{
    DllAddRef();
}

CLangBarVariantButton::~CLangBarVariantButton()
{
    if (_hIconSimplified)  { DestroyIcon(_hIconSimplified);  _hIconSimplified = nullptr; }
    if (_hIconTraditional) { DestroyIcon(_hIconTraditional); _hIconTraditional = nullptr; }
    DllRelease();
}

STDMETHODIMP CLangBarVariantButton::QueryInterface(REFIID riid, void** ppvObj)
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

STDMETHODIMP_(ULONG) CLangBarVariantButton::AddRef()  { return InterlockedIncrement(&_cRef); }
STDMETHODIMP_(ULONG) CLangBarVariantButton::Release()
{
    LONG c = InterlockedDecrement(&_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CLangBarVariantButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = CLSID_M2CTextService;
    pInfo->guidItem = GUID_M2CLangBarVariantButton;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;  // 非菜单式，直接点击
    pInfo->ulSort = 3;  // 排在模式、全角/半角、标点按钮后面
    wcscpy_s(pInfo->szDescription, M2C_VARIANT_BTN_DESC);
    return S_OK;
}

STDMETHODIMP CLangBarVariantButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarVariantButton::Show(BOOL /*fShow*/) { return S_OK; }

STDMETHODIMP CLangBarVariantButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (!pbstrToolTip) return E_INVALIDARG;
    const WCHAR* tip = M2C_TIP_SIMPLIFIED;
    if (_pService && _pService->GetChineseVariant() == IME_VARIANT_TRADITIONAL)
        tip = M2C_TIP_TRADITIONAL;
    *pbstrToolTip = SysAllocString(tip);
    return (*pbstrToolTip) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarVariantButton::OnClick(TfLBIClick click, POINT /*pt*/, const RECT* /*prcArea*/)
{
    // 直接点击切换简体/繁体
    (void)click;
    if (_pService)
        _pService->ToggleChineseVariant();
    return S_OK;
}

STDMETHODIMP CLangBarVariantButton::InitMenu(ITfMenu* pMenu)
{
    (void)pMenu;
    return S_OK;
}

STDMETHODIMP CLangBarVariantButton::OnMenuSelect(UINT /*wID*/)
{
    return S_OK;
}

STDMETHODIMP CLangBarVariantButton::GetIcon(HICON* phIcon)
{
    if (!phIcon) return E_INVALIDARG;
    *phIcon = _GetVariantIcon();
    return (*phIcon) ? S_OK : E_FAIL;
}

HICON CLangBarVariantButton::_GetVariantIcon()
{
    ImeChineseVariant variant = _pService ? _pService->GetChineseVariant() : IME_VARIANT_SIMPLIFIED;

    if (variant == IME_VARIANT_TRADITIONAL)
    {
        // 繁体：蓝色图标
        if (_hIconTraditional == nullptr)
            _hIconTraditional = _CreateVariantIcon(L"繁", RGB(0, 95, 184));
        return _hIconTraditional;
    }
    else
    {
        // 简体：灰色图标
        if (_hIconSimplified == nullptr)
            _hIconSimplified = _CreateVariantIcon(L"简", RGB(90, 90, 90));
        return _hIconSimplified;
    }
}

// 用 GDI 画一个 16x16 的圆角小方块，中间是白色字
HICON CLangBarVariantButton::_CreateVariantIcon(const WCHAR* text, COLORREF bg)
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

STDMETHODIMP CLangBarVariantButton::GetText(BSTR* pbstrText)
{
    if (!pbstrText) return E_INVALIDARG;
    const WCHAR* text = L"简";
    if (_pService && _pService->GetChineseVariant() == IME_VARIANT_TRADITIONAL)
        text = L"繁";
    *pbstrText = SysAllocString(text);
    return (*pbstrText) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarVariantButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (_pSink) return CONNECT_E_ADVISELIMIT;
    if (!punk || !pdwCookie) return E_INVALIDARG;

    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&_pSink)))
        return E_NOINTERFACE;

    _dwSinkCookie = 0x56415249; // 'VARI'
    *pdwCookie = _dwSinkCookie;
    return S_OK;
}

STDMETHODIMP CLangBarVariantButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != _dwSinkCookie || !_pSink)
        return CONNECT_E_NOCONNECTION;
    _pSink->Release();
    _pSink = nullptr;
    _dwSinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}

void CLangBarVariantButton::_NotifyUpdate()
{
    if (_pSink)
        _pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

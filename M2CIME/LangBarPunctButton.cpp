// LangBarPunctButton.cpp —— 语言栏全角/半角标点切换按钮
#include "Globals.h"
#include "LangBarPunctButton.h"
#include "TextService.h"

CLangBarPunctButton::CLangBarPunctButton(CTextService* pService)
    : _cRef(1)
    , _pService(pService)
    , _pSink(nullptr)
    , _dwSinkCookie(TF_INVALID_COOKIE)
    , _hIconFull(nullptr)
    , _hIconHalf(nullptr)
{
    DllAddRef();
}

CLangBarPunctButton::~CLangBarPunctButton()
{
    if (_hIconFull) { DestroyIcon(_hIconFull); _hIconFull = nullptr; }
    if (_hIconHalf) { DestroyIcon(_hIconHalf); _hIconHalf = nullptr; }
    DllRelease();
}

STDMETHODIMP CLangBarPunctButton::QueryInterface(REFIID riid, void** ppvObj)
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

STDMETHODIMP_(ULONG) CLangBarPunctButton::AddRef()  { return InterlockedIncrement(&_cRef); }
STDMETHODIMP_(ULONG) CLangBarPunctButton::Release()
{
    LONG c = InterlockedDecrement(&_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CLangBarPunctButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = CLSID_M2CTextService;
    pInfo->guidItem = GUID_M2CLangBarPunctButton;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;  // 非菜单式，直接点击
    pInfo->ulSort = 1;  // 排在模式按钮后面
    wcscpy_s(pInfo->szDescription, M2C_PUNCT_BTN_DESC);
    return S_OK;
}

STDMETHODIMP CLangBarPunctButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarPunctButton::Show(BOOL /*fShow*/) { return S_OK; }

STDMETHODIMP CLangBarPunctButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (!pbstrToolTip) return E_INVALIDARG;
    const WCHAR* tip = M2C_TIP_HALFWIDTH;
    if (_pService && _pService->GetPunctMode() == IME_PUNCT_FULLWIDTH)
        tip = M2C_TIP_FULLWIDTH;
    *pbstrToolTip = SysAllocString(tip);
    return (*pbstrToolTip) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarPunctButton::OnClick(TfLBIClick click, POINT /*pt*/, const RECT* /*prcArea*/)
{
    // 直接点击切换全角/半角
    (void)click;
    if (_pService)
        _pService->TogglePunctMode();
    return S_OK;
}

STDMETHODIMP CLangBarPunctButton::InitMenu(ITfMenu* pMenu)
{
    // 非菜单式按钮，不提供菜单
    (void)pMenu;
    return S_OK;
}

STDMETHODIMP CLangBarPunctButton::OnMenuSelect(UINT /*wID*/)
{
    return S_OK;
}

STDMETHODIMP CLangBarPunctButton::GetIcon(HICON* phIcon)
{
    if (!phIcon) return E_INVALIDARG;
    *phIcon = _GetPunctModeIcon();
    return (*phIcon) ? S_OK : E_FAIL;
}

HICON CLangBarPunctButton::_GetPunctModeIcon()
{
    ImePunctMode mode = _pService ? _pService->GetPunctMode() : IME_PUNCT_HALFWIDTH;

    if (mode == IME_PUNCT_FULLWIDTH)
    {
        if (_hIconFull == nullptr)
            _hIconFull = _CreatePunctModeIcon(L"\u25B6", RGB(0, 120, 0));   // ▶  绿色：全角
        return _hIconFull;
    }
    else
    {
        if (_hIconHalf == nullptr)
            _hIconHalf = _CreatePunctModeIcon(L"\u25B7", RGB(120, 120, 0)); // ▷  暗黄色：半角
        return _hIconHalf;
    }
}

// 用 GDI 画一个 16x16 的圆角小方块，中间是白色字
HICON CLangBarPunctButton::_CreatePunctModeIcon(const WCHAR* text, COLORREF bg)
{
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = cx;
    bi.bV5Height      = -cy;             // 负高度：自上而下
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

    // 掩码位图必须全部为 0（不透明），否则会出现随机透明块
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
                             FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI Symbol");
    HGDIOBJ oldFont = SelectObject(memDC, font);
    RECT rc = { 0, 0, cx, cy };
    DrawTextW(memDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // GDI 不写 alpha 通道：非黑像素按不透明处理
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

STDMETHODIMP CLangBarPunctButton::GetText(BSTR* pbstrText)
{
    if (!pbstrText) return E_INVALIDARG;
    const WCHAR* text = L"\u25B7";   // ▷
    if (_pService && _pService->GetPunctMode() == IME_PUNCT_FULLWIDTH)
        text = L"\u25B6";             // ▶
    *pbstrText = SysAllocString(text);
    return (*pbstrText) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarPunctButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (_pSink) return CONNECT_E_ADVISELIMIT;
    if (!punk || !pdwCookie) return E_INVALIDARG;

    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&_pSink)))
        return E_NOINTERFACE;

    _dwSinkCookie = 0x504E4354; // 'PNCT'
    *pdwCookie = _dwSinkCookie;
    return S_OK;
}

STDMETHODIMP CLangBarPunctButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != _dwSinkCookie || !_pSink)
        return CONNECT_E_NOCONNECTION;
    _pSink->Release();
    _pSink = nullptr;
    _dwSinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}

void CLangBarPunctButton::_NotifyUpdate()
{
    if (_pSink)
        _pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

// LangBarButton.cpp —— 语言栏模式切换按钮
#include "Globals.h"
#include "LangBarButton.h"
#include "TextService.h"

CLangBarButton::CLangBarButton(CTextService* pService)
    : _cRef(1)
    , _pService(pService)
    , _pSink(nullptr)
    , _dwSinkCookie(TF_INVALID_COOKIE)
    , _hIcon{ nullptr, nullptr, nullptr }
{
    // 注意：不对 _pService AddRef，按钮生命周期由 TextService 管理
    DllAddRef();
}

CLangBarButton::~CLangBarButton()
{
    for (HICON& h : _hIcon)
    {
        if (h) { DestroyIcon(h); h = nullptr; }
    }
    DllRelease();
}

STDMETHODIMP CLangBarButton::QueryInterface(REFIID riid, void** ppvObj)
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

STDMETHODIMP_(ULONG) CLangBarButton::AddRef()  { return InterlockedIncrement(&_cRef); }
STDMETHODIMP_(ULONG) CLangBarButton::Release()
{
    LONG c = InterlockedDecrement(&_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CLangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = CLSID_M2CTextService;
    pInfo->guidItem = GUID_M2CLangBarButton;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 0;
    wcscpy_s(pInfo->szDescription, M2C_LANGBAR_DESC);
    return S_OK;
}

STDMETHODIMP CLangBarButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarButton::Show(BOOL /*fShow*/) { return S_OK; }

STDMETHODIMP CLangBarButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (!pbstrToolTip) return E_INVALIDARG;
    *pbstrToolTip = SysAllocString(M2C_LANGBAR_TIP);
    return (*pbstrToolTip) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarButton::OnClick(TfLBIClick click, POINT /*pt*/, const RECT* /*prcArea*/)
{
    // 菜单式按钮：点击后由语言栏调用 InitMenu 弹出模式菜单，此处无需处理
    (void)click;
    return S_OK;
}

STDMETHODIMP CLangBarButton::InitMenu(ITfMenu* pMenu)
{
    if (!pMenu) return E_INVALIDARG;

    ImeInputMode cur = _pService ? _pService->GetMode() : IME_MODE_MANDARIN;

    struct MenuItem { UINT id; LPCWSTR text; ImeInputMode mode; };
    static const MenuItem kItems[] =
    {
        { 1, L"英文",   IME_MODE_ENGLISH   },
        { 2, L"普通话", IME_MODE_MANDARIN  },
        { 3, L"粤语",   IME_MODE_CANTONESE },
    };

    for (const MenuItem& it : kItems)
    {
        DWORD flags = (it.mode == cur) ? TF_LBMENUF_RADIOCHECKED : 0;
        pMenu->AddMenuItem(it.id, flags, nullptr, nullptr, it.text,
                           (ULONG)wcslen(it.text), nullptr);
    }
    return S_OK;
}

STDMETHODIMP CLangBarButton::OnMenuSelect(UINT wID)
{
    if (!_pService) return S_OK;

    switch (wID)
    {
    case 1: _pService->SetMode(IME_MODE_ENGLISH);   break;
    case 2: _pService->SetMode(IME_MODE_MANDARIN);  break;
    case 3: _pService->SetMode(IME_MODE_CANTONESE); break;
    }
    return S_OK;
}

STDMETHODIMP CLangBarButton::GetIcon(HICON* phIcon)
{
    if (!phIcon) return E_INVALIDARG;
    *phIcon = _GetModeIcon();
    return (*phIcon) ? S_OK : E_FAIL;
}

HICON CLangBarButton::_GetModeIcon()
{
    ImeInputMode mode = _pService ? _pService->GetMode() : IME_MODE_MANDARIN;
    int idx = (int)mode;
    if (idx < 0 || idx > 2) idx = (int)IME_MODE_MANDARIN;

    if (_hIcon[idx] == nullptr)
    {
        const WCHAR* text = L"普";
        COLORREF bg = RGB(0, 95, 184);       // 中文模式：蓝底
        switch (mode)
        {
        case IME_MODE_ENGLISH:   text = L"英"; bg = RGB(90, 90, 90); break;
        case IME_MODE_MANDARIN:  text = L"普"; break;
        case IME_MODE_CANTONESE: text = L"粤"; break;
        }
        _hIcon[idx] = _CreateModeIcon(text, bg);
    }
    return _hIcon[idx];
}

// 语言栏按钮只按图标绘制，用 GDI 画一个 16x16 的圆角小方块，中间是白色模式字
HICON CLangBarButton::_CreateModeIcon(const WCHAR* text, COLORREF bg)
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
    HFONT font = CreateFontW(-MulDiv(10, GetDeviceCaps(hdcScreen, LOGPIXELSY), 72), 0, 0, 0,
                             FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(memDC, font);
    RECT rc = { 0, 0, cx, cy };
    DrawTextW(memDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // GDI 不写 alpha 通道：非黑像素按不透明处理，圆角外仍保持透明
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

STDMETHODIMP CLangBarButton::GetText(BSTR* pbstrText)
{
    if (!pbstrText) return E_INVALIDARG;
    const WCHAR* text = L"普";
    if (_pService)
    {
        switch (_pService->GetMode())
        {
        case IME_MODE_ENGLISH:   text = L"英"; break;
        case IME_MODE_MANDARIN:  text = L"普"; break;
        case IME_MODE_CANTONESE: text = L"粤"; break;
        }
    }
    *pbstrText = SysAllocString(text);
    return (*pbstrText) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (_pSink) return CONNECT_E_ADVISELIMIT;
    if (!punk || !pdwCookie) return E_INVALIDARG;

    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&_pSink)))
        return E_NOINTERFACE;

    _dwSinkCookie = 0x4D324349; // 'M2CI'
    *pdwCookie = _dwSinkCookie;
    return S_OK;
}

STDMETHODIMP CLangBarButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != _dwSinkCookie || !_pSink)
        return CONNECT_E_NOCONNECTION;
    _pSink->Release();
    _pSink = nullptr;
    _dwSinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}

void CLangBarButton::_NotifyUpdate()
{
    if (_pSink)
        _pSink->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

// CandidateWindow.cpp —— 自绘候选词窗口
#include "Globals.h"
#include "CandidateWindow.h"
#include "TextService.h"

static const WCHAR* kWndClass = L"M2CCandidateWindow";

// 配色
static const COLORREF kColBg        = RGB(255, 255, 255);
static const COLORREF kColBorder    = RGB(188, 188, 188);
static const COLORREF kColPinyin    = RGB(90, 90, 90);
static const COLORREF kColText      = RGB(30, 30, 30);
static const COLORREF kColHighlight = RGB(0, 95, 184);   // 选中项蓝底
static const COLORREF kColHiText    = RGB(255, 255, 255);
static const COLORREF kColSourceTag = RGB(150, 150, 150);

CCandidateWindow::CCandidateWindow(CTextService* pService)
    : _pService(pService)
    , _cRef(1)
    , _hwnd(nullptr)
    , _pageIndex(0)
    , _totalPages(1)
    , _hoverIndex(-1)
    , _fontPinyin(nullptr)
    , _fontItem(nullptr)
{
}

CCandidateWindow::~CCandidateWindow()
{
    _Destroy();
}

void CCandidateWindow::_EnsureWindow()
{
    if (_hwnd) return;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = _WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    _hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWndClass, L"",
        WS_POPUP | WS_BORDER,
        0, 0, kMinWidth, 100,
        nullptr, nullptr, g_hInst, this);

    if (_hwnd)
    {
        _fontPinyin = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        _fontItem = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    }
}

void CCandidateWindow::_Destroy()
{
    if (_hwnd)
    {
        DestroyWindow(_hwnd);
        _hwnd = nullptr;
    }
    if (_fontPinyin) { DeleteObject(_fontPinyin); _fontPinyin = nullptr; }
    if (_fontItem)   { DeleteObject(_fontItem);   _fontItem = nullptr; }
}

void CCandidateWindow::_Hide()
{
    if (_hwnd && IsWindowVisible(_hwnd))
        ShowWindow(_hwnd, SW_HIDE);
    _hoverIndex = -1;
}

void CCandidateWindow::_RecalcLayout()
{
    _EnsureWindow();

    HDC hdc = GetDC(_hwnd);
    HGDIOBJ oldFont = SelectObject(hdc, _fontItem);

    // 计算每项所需宽度
    int maxW = kMinWidth;
    WCHAR line[80];
    for (size_t i = 0; i < _items.size(); ++i)
    {
        swprintf_s(line, L"%d. %s%s", (int)i + 1, _items[i].first.c_str(),
                   _items[i].second == 1 ? L"  粤" : L"");
        SIZE sz = {};
        GetTextExtentPoint32W(hdc, line, (int)wcslen(line), &sz);
        if (sz.cx + kPadding * 2 > maxW) maxW = sz.cx + kPadding * 2;
    }
    SelectObject(hdc, _fontPinyin);
    SIZE szPy = {};
    GetTextExtentPoint32W(hdc, _pinyin.c_str(), (int)_pinyin.size(), &szPy);
    if (szPy.cx + kPadding * 2 > maxW) maxW = szPy.cx + kPadding * 2;
    ReleaseDC(_hwnd, hdc);

    if (maxW > kMaxWidth) maxW = kMaxWidth;

    int itemCount = (int)_items.size();
    int height = kPadding + kPinyinHeight + 4 +
                 itemCount * kItemHeight +
                 ((_totalPages > 1) ? kPageInfoHeight : 0) + kPadding - 4;

    int y = kPadding;
    SetRect(&_rcPinyin, kPadding, y, maxW - kPadding, y + kPinyinHeight);
    y += kPinyinHeight + 4;
    for (int i = 0; i < itemCount; ++i)
    {
        SetRect(&_rcItems[i], 4, y, maxW - 4, y + kItemHeight);
        y += kItemHeight;
    }
    if (_totalPages > 1)
    {
        SetRect(&_rcPageInfo, kPadding, y, maxW - kPadding, y + kPageInfoHeight);
    }
    else
    {
        SetRectEmpty(&_rcPageInfo);
    }

    SetWindowPos(_hwnd, nullptr, 0, 0, maxW, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CCandidateWindow::_Update(const std::wstring& pinyin,
                               const std::vector<std::pair<std::wstring, int>>& items,
                               int pageIndex, int totalPages,
                               const RECT* caretRect)
{
    _pinyin = pinyin;
    _items = items;
    _pageIndex = pageIndex;
    _totalPages = totalPages;
    _hoverIndex = -1;

    _RecalcLayout();

    // 定位：光标左下角
    int x = caretRect ? caretRect->left : 100;
    int y = caretRect ? caretRect->bottom + 4 : 100;

    RECT wrc;
    GetWindowRect(_hwnd, &wrc);
    int w = wrc.right - wrc.left;
    int h = wrc.bottom - wrc.top;

    // 屏幕边界调整（多显示器取光标所在屏）
    HMONITOR hMon = MonitorFromPoint(POINT{ x, y }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);
    if (x + w > mi.rcWork.right)  x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = (caretRect ? caretRect->top : y) - h - 4; // 翻到光标上方
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;

    SetWindowPos(_hwnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(_hwnd, nullptr, FALSE);
}

int CCandidateWindow::_HitTest(int y) const
{
    for (int i = 0; i < (int)_items.size(); ++i)
    {
        if (y >= _rcItems[i].top && y < _rcItems[i].bottom)
            return i;
    }
    return -1;
}

LRESULT CCandidateWindow::_OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(_hwnd, &ps);

    RECT rcClient;
    GetClientRect(_hwnd, &rcClient);

    // 双缓冲
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    // 背景与边框
    HBRUSH bgBrush = CreateSolidBrush(kColBg);
    FillRect(memDC, &rcClient, bgBrush);
    DeleteObject(bgBrush);
    HPEN pen = CreatePen(PS_SOLID, 1, kColBorder);
    HGDIOBJ oldPen = SelectObject(memDC, pen);
    HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, rcClient.left, rcClient.top, rcClient.right - 1, rcClient.bottom - 1);
    SelectObject(memDC, oldBrush);
    SelectObject(memDC, oldPen);
    DeleteObject(pen);

    SetBkMode(memDC, TRANSPARENT);

    // 拼音行
    HGDIOBJ oldFont = SelectObject(memDC, _fontPinyin);
    SetTextColor(memDC, kColPinyin);
    DrawTextW(memDC, _pinyin.c_str(), (int)_pinyin.size(), (RECT*)&_rcPinyin,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 候选项
    SelectObject(memDC, _fontItem);
    for (int i = 0; i < (int)_items.size(); ++i)
    {
        bool fHighlight = (i == 0 && _hoverIndex < 0) || (i == _hoverIndex);
        if (fHighlight)
        {
            HBRUSH hiBrush = CreateSolidBrush(kColHighlight);
            FillRect(memDC, &_rcItems[i], hiBrush);
            DeleteObject(hiBrush);
            SetTextColor(memDC, kColHiText);
        }
        else
        {
            SetTextColor(memDC, kColText);
        }

        RECT rcText = _rcItems[i];
        rcText.left += kPadding - 4;
        WCHAR line[80];
        swprintf_s(line, L"%d. %s", i + 1, _items[i].first.c_str());
        DrawTextW(memDC, line, (int)wcslen(line), &rcText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 粤语来源标记
        if (_items[i].second == 1)
        {
            int w = rcText.right - rcText.left;
            RECT rcTag = rcText;
            rcTag.left = rcText.right - 28;
            SetTextColor(memDC, fHighlight ? kColHiText : kColSourceTag);
            DrawTextW(memDC, L"粤", 1, &rcTag, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            (void)w;
        }
    }

    // 页码
    if (_totalPages > 1)
    {
        SelectObject(memDC, _fontPinyin);
        SetTextColor(memDC, kColSourceTag);
        WCHAR page[32];
        swprintf_s(page, L"%d/%d", _pageIndex + 1, _totalPages);
        RECT rcP = _rcPageInfo;
        rcP.right -= 4;
        DrawTextW(memDC, page, (int)wcslen(page), &rcP,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(memDC, oldFont);
    BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(_hwnd, &ps);
    return 0;
}

LRESULT CCandidateWindow::_OnMouseMove(int x, int y)
{
    int idx = _HitTest(y);
    if (idx != _hoverIndex)
    {
        _hoverIndex = idx;
        InvalidateRect(_hwnd, nullptr, FALSE);
    }
    // 追踪鼠标离开
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = _hwnd;
    TrackMouseEvent(&tme);
    (void)x;
    return 0;
}

LRESULT CCandidateWindow::_OnLButtonUp(int /*x*/, int y)
{
    int idx = _HitTest(y);
    if (idx >= 0 && _pService)
        _pService->OnCandidateClicked(idx);
    return 0;
}

LRESULT CALLBACK CCandidateWindow::_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CCandidateWindow* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (CCandidateWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->_hwnd = hwnd;
    }
    else
    {
        pThis = (CCandidateWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (!pThis) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_PAINT:       return pThis->_OnPaint();
    case WM_MOUSEMOVE:   return pThis->_OnMouseMove(LOWORD(lParam), HIWORD(lParam));
    case WM_LBUTTONUP:   return pThis->_OnLButtonUp(LOWORD(lParam), HIWORD(lParam));
    case WM_MOUSELEAVE:
        pThis->_hoverIndex = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:  return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// StatusBar.cpp —— 输入法自带状态条（英/普/粤 切换）
#include "Globals.h"
#include "StatusBar.h"
#include "TextService.h"

static const WCHAR* kStatusClass = L"M2CStatusBar";

static const COLORREF kBg        = RGB(252, 252, 252);
static const COLORREF kBorder    = RGB(206, 206, 206);
static const COLORREF kBgHover   = RGB(240, 246, 253);
static const COLORREF kTextCn    = RGB(0, 95, 184);
static const COLORREF kTextEn    = RGB(90, 90, 90);
static const COLORREF kArrow     = RGB(140, 140, 140);

CStatusBar::CStatusBar(CTextService* pService)
    : _pService(pService), _hwnd(nullptr), _fontMode(nullptr), _fontArrow(nullptr), _hover(false)
{
}

CStatusBar::~CStatusBar()
{
    _Destroy();
}

void CStatusBar::_EnsureWindow()
{
    if (_hwnd) return;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = _WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.lpszClassName = kStatusClass;
    RegisterClassExW(&wc);

    _hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kStatusClass, L"",
        WS_POPUP,
        0, 0, kWidth, kHeight,
        nullptr, nullptr, g_hInst, this);

    if (_hwnd)
    {
        _fontMode = CreateFontW(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        _fontArrow = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        _Reposition();
    }
}

void CStatusBar::_Reposition()
{
    if (!_hwnd) return;

    // 停靠在主屏工作区右下角（任务栏之上）
    HMONITOR hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);

    int x = mi.rcWork.right - kWidth - 6;
    int y = mi.rcWork.bottom - kHeight - 6;

    SetWindowPos(_hwnd, HWND_TOPMOST, x, y, kWidth, kHeight, SWP_NOACTIVATE);
}

void CStatusBar::_Show()
{
    _EnsureWindow();
    if (!_hwnd) return;
    _Reposition();
    ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
    InvalidateRect(_hwnd, nullptr, FALSE);
}

void CStatusBar::_Hide()
{
    if (_hwnd && IsWindowVisible(_hwnd))
        ShowWindow(_hwnd, SW_HIDE);
}

void CStatusBar::_Update()
{
    if (_hwnd && IsWindowVisible(_hwnd))
        InvalidateRect(_hwnd, nullptr, FALSE);
}

void CStatusBar::_Destroy()
{
    if (_hwnd)
    {
        DestroyWindow(_hwnd);
        _hwnd = nullptr;
    }
    if (_fontMode)  { DeleteObject(_fontMode);  _fontMode = nullptr; }
    if (_fontArrow) { DeleteObject(_fontArrow); _fontArrow = nullptr; }
}

LRESULT CStatusBar::_OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(_hwnd, &ps);

    RECT rc;
    GetClientRect(_hwnd, &rc);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    // 背景（悬停时浅蓝）
    HBRUSH brush = CreateSolidBrush(_hover ? kBgHover : kBg);
    FillRect(memDC, &rc, brush);
    DeleteObject(brush);

    // 边框
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldPen = SelectObject(memDC, pen);
    HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, rc.right - 1, rc.bottom - 1);
    SelectObject(memDC, oldBrush);
    SelectObject(memDC, oldPen);
    DeleteObject(pen);

    SetBkMode(memDC, TRANSPARENT);

    ImeInputMode mode = _pService ? _pService->GetMode() : IME_MODE_MANDARIN;
    LPCWSTR text = L"普";
    COLORREF color = kTextCn;
    switch (mode)
    {
    case IME_MODE_ENGLISH:   text = L"英"; color = kTextEn; break;
    case IME_MODE_MANDARIN:  text = L"普"; color = kTextCn; break;
    case IME_MODE_CANTONESE: text = L"粤"; color = kTextCn; break;
    }

    // 模式字
    SelectObject(memDC, _fontMode);
    SetTextColor(memDC, color);
    RECT rcText = rc;
    rcText.right -= 22;
    DrawTextW(memDC, text, 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ▾
    SelectObject(memDC, _fontArrow);
    SetTextColor(memDC, kArrow);
    RECT rcArrow = rc;
    rcArrow.left = rc.right - 24;
    rcArrow.right = rc.right - 8;
    DrawTextW(memDC, L"▼", 1, &rcArrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(_hwnd, &ps);
    return 0;
}

void CStatusBar::_OnLButtonUp()
{
    if (!_pService) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    ImeInputMode cur = _pService->GetMode();
    AppendMenuW(menu, MF_STRING | (cur == IME_MODE_ENGLISH   ? MF_CHECKED : 0), 1, L"英文");
    AppendMenuW(menu, MF_STRING | (cur == IME_MODE_MANDARIN  ? MF_CHECKED : 0), 2, L"普通话");
    AppendMenuW(menu, MF_STRING | (cur == IME_MODE_CANTONESE ? MF_CHECKED : 0), 3, L"粤语");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(_hwnd);

    UINT cmd = (UINT)TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
        pt.x, pt.y, 0, _hwnd, nullptr);
    PostMessage(_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd >= 1 && cmd <= 3)
    {
        // 1=英文(0) 2=普通话(1) 3=粤语(2)
        _pService->SetMode((ImeInputMode)(cmd - 1));
        _Update();
    }
}

LRESULT CALLBACK CStatusBar::_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CStatusBar* pThis = (CStatusBar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (CStatusBar*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        if (pThis) pThis->_hwnd = hwnd;
    }
    if (!pThis) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_PAINT:
        return pThis->_OnPaint();
    case WM_LBUTTONUP:
        pThis->_OnLButtonUp();
        return 0;
    case WM_MOUSEMOVE:
        if (!pThis->_hover)
        {
            pThis->_hover = true;
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        pThis->_hover = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

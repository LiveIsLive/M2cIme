// StatusBar.cpp —— 输入法自带状态条（英/普/粤 切换）+ 全角/半角状态显示
#include "Globals.h"
#include "StatusBar.h"
#include "TextService.h"
#include <windowsx.h>
#include <commctrl.h>

static const WCHAR* kStatusClass = L"M2CStatusBar";

static const COLORREF kBg        = RGB(252, 252, 252);
static const COLORREF kBorder    = RGB(206, 206, 206);
static const COLORREF kBgHover   = RGB(240, 246, 253);
static const COLORREF kTextCn    = RGB(0, 95, 184);
static const COLORREF kTextEn    = RGB(90, 90, 90);
static const COLORREF kArrow     = RGB(140, 140, 140);

CStatusBar::CStatusBar(CTextService* pService)
    : _pService(pService), _hwnd(nullptr), _hwndTip(nullptr), _fontMode(nullptr), _fontArrow(nullptr),
      _fontPunct(nullptr), _hover(false), _tipMode{}, _tipPunct{}
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
        _fontPunct = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        _Reposition();
        _EnsureTooltip();
    }
}

// 创建悬停提示：模式区、全角/半角区各一个工具项，文字随当前状态变化
void CStatusBar::_EnsureTooltip()
{
    if (_hwndTip || !_hwnd) return;

    // 必须先注册通用控件类（tooltips_class32），否则创建会失败
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    // 状态条永不激活，必须带 TTS_ALWAYSTIP 才能弹出提示
    _hwndTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               0, 0, 0, 0, _hwnd, nullptr, g_hInst, nullptr);
    if (!_hwndTip) return;

    SendMessageW(_hwndTip, TTM_SETMAXTIPWIDTH, 0, 600);

    TOOLINFO ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd = _hwnd;

    ti.uId = 1;
    ti.rect.left = 0;
    ti.rect.top = 0;
    ti.rect.right = kModeRight;
    ti.rect.bottom = kHeight;
    ti.lpszText = _tipMode;
    SendMessageW(_hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = 2;
    ti.rect.left = kModeRight + 1;
    ti.rect.right = kWidth;
    ti.lpszText = _tipPunct;
    SendMessageW(_hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    _UpdateTooltipText();
}

void CStatusBar::_UpdateTooltipText()
{
    ImeInputMode mode = _pService ? _pService->GetMode() : IME_MODE_MANDARIN;
    LPCWSTR modeText = M2C_TIP_MANDARIN;
    switch (mode)
    {
    case IME_MODE_ENGLISH:   modeText = M2C_TIP_ENGLISH;   break;
    case IME_MODE_CANTONESE: modeText = M2C_TIP_CANTONESE; break;
    }
    wcscpy_s(_tipMode, modeText);

    ImePunctMode punctMode = _pService ? _pService->GetPunctMode() : IME_PUNCT_HALFWIDTH;
    wcscpy_s(_tipPunct, punctMode == IME_PUNCT_FULLWIDTH ? M2C_TIP_FULLWIDTH : M2C_TIP_HALFWIDTH);

    if (!_hwndTip) return;

    TOOLINFO ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd = _hwnd;

    ti.uId = 1;
    ti.lpszText = _tipMode;
    SendMessageW(_hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

    ti.uId = 2;
    ti.lpszText = _tipPunct;
    SendMessageW(_hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

// 状态条不参与激活，需手动把鼠标消息转发给提示窗口
void CStatusBar::_RelayMouseToTooltip(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!_hwndTip) return;

    MSG relay = {};
    relay.hwnd = _hwnd;
    relay.message = msg;
    relay.wParam = wParam;
    relay.lParam = lParam;
    relay.time = GetTickCount();
    relay.pt.x = GET_X_LPARAM(lParam);
    relay.pt.y = GET_Y_LPARAM(lParam);
    ClientToScreen(_hwnd, &relay.pt);
    SendMessageW(_hwndTip, TTM_RELAYEVENT, 0, (LPARAM)&relay);
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
    _UpdateTooltipText();
    if (_hwnd && IsWindowVisible(_hwnd))
        InvalidateRect(_hwnd, nullptr, FALSE);
}

void CStatusBar::_Destroy()
{
    if (_hwnd)
    {
        DestroyWindow(_hwnd);   // 提示窗口作为子窗口一并销毁
        _hwnd = nullptr;
        _hwndTip = nullptr;
    }
    if (_fontMode)  { DeleteObject(_fontMode);  _fontMode = nullptr; }
    if (_fontArrow) { DeleteObject(_fontArrow); _fontArrow = nullptr; }
    if (_fontPunct) { DeleteObject(_fontPunct); _fontPunct = nullptr; }
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
    ImePunctMode punctMode = _pService ? _pService->GetPunctMode() : IME_PUNCT_HALFWIDTH;
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
    rcText.right = 40;  // 模式字占左边 40px
    DrawTextW(memDC, text, 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 分隔线
    HPEN sepPen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldSepPen = SelectObject(memDC, sepPen);
    MoveToEx(memDC, 38, 4, nullptr);
    LineTo(memDC, 38, rc.bottom - 5);
    SelectObject(memDC, oldSepPen);
    DeleteObject(sepPen);

    // 全角/半角状态符号（单字，更紧凑）
    LPCWSTR punctText = (punctMode == IME_PUNCT_FULLWIDTH) ? L"\u25B6" : L"\u25B7";
    COLORREF punctColor = (punctMode == IME_PUNCT_FULLWIDTH) ? RGB(0, 120, 0) : RGB(120, 120, 0);
    SelectObject(memDC, _fontPunct);
    SetTextColor(memDC, punctColor);
    RECT rcPunct = rc;
    rcPunct.left = 40;
    rcPunct.right = rc.right - 22;
    DrawTextW(memDC, punctText, 1, &rcPunct, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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

void CStatusBar::_OnLButtonUp(LPARAM lParam)
{
    if (!_pService) return;

    // 获取点击位置（窗口客户区坐标）
    int clickX = GET_X_LPARAM(lParam);

    // 左侧 0-39px 是模式字区域（含分隔线），点击弹出模式菜单
    // 右侧 40px 以后是标点状态区域，点击切换全角/半角
    if (clickX >= 40)
    {
        _pService->TogglePunctMode();
        _Update();
        return;
    }

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
        pThis->_OnLButtonUp(lParam);
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
        // 转发鼠标移动，让提示窗口按所在区域弹出“模式/全角半角”提示
        pThis->_RelayMouseToTooltip(WM_MOUSEMOVE, wParam, lParam);
        return 0;
    case WM_MOUSELEAVE:
        pThis->_hover = false;
        if (pThis->_hwndTip)
            SendMessageW(pThis->_hwndTip, TTM_POP, 0, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

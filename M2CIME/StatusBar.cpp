// StatusBar.cpp —— 输入法自带状态条（英/普/粤 切换）+ 全角/半角、标点、简/繁状态显示
#include "Globals.h"
#include "StatusBar.h"
#include "TextService.h"
#include <windowsx.h>
#include <commctrl.h>

static const WCHAR* kStatusClass = L"M2CStatusBar";

// 状态条位置持久化（与模式等设置放在同一个注册表键下）
static const WCHAR* kRegKey  = L"Software\\M2CIME";
static const WCHAR* kRegPosX = L"StatusBarX";
static const WCHAR* kRegPosY = L"StatusBarY";

// 拖动轮询定时器（SetCapture 在非激活窗口上可能不可靠，用定时器兜底）
static const UINT_PTR kDragTimerId = 1;
static const UINT kDragTimerMs = 15;

static const COLORREF kBg        = RGB(252, 252, 252);
static const COLORREF kBorder    = RGB(206, 206, 206);
static const COLORREF kBgHover   = RGB(240, 246, 253);
static const COLORREF kTextCn    = RGB(0, 95, 184);
static const COLORREF kTextEn    = RGB(90, 90, 90);
static const COLORREF kArrow     = RGB(140, 140, 140);

CStatusBar::CStatusBar(CTextService* pService)
    : _pService(pService), _hwnd(nullptr), _hwndTip(nullptr), _fontMode(nullptr), _fontArrow(nullptr),
      _fontPunct(nullptr), _fontWidth(nullptr), _fontVariant(nullptr), _fontSettings(nullptr), _hover(false),
      _dragging(false), _dragMoved(false), _suppressClick(false),
      _dragStart{}, _dragOrigin{},
      _hasCustomPos(false), _posX(0), _posY(0),
      _tipMode{}, _tipPunct{}, _tipWidth{}, _tipVariant{}, _tipSettings{}
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
        _fontWidth = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        _fontVariant = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        _fontSettings = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
        _Reposition();
        _EnsureTooltip();
    }
}

// 创建悬停提示：模式、全角/半角、标点、简/繁各一个工具项，文字随当前状态变化
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
    ti.rect.right = kWidthRight;
    ti.lpszText = _tipWidth;
    SendMessageW(_hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = 3;
    ti.rect.left = kWidthRight + 1;
    ti.rect.right = kPunctRight;
    ti.lpszText = _tipPunct;
    SendMessageW(_hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = 4;
    ti.rect.left = kPunctRight + 1;
    ti.rect.right = kVariantRight;
    ti.lpszText = _tipVariant;
    SendMessageW(_hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = 5;
    ti.rect.left = kVariantRight + 1;
    ti.rect.right = kSettingsRight;
    ti.lpszText = _tipSettings;
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
    wcscpy_s(_tipPunct, punctMode == IME_PUNCT_FULLWIDTH ? M2C_TIP_CN_PUNCT : M2C_TIP_EN_PUNCT);

    ImeWidthMode widthMode = _pService ? _pService->GetWidthMode() : IME_WIDTH_HALFWIDTH;
    wcscpy_s(_tipWidth, widthMode == IME_WIDTH_FULLWIDTH ? M2C_TIP_FULLWIDTH : M2C_TIP_HALFWIDTH);

    ImeChineseVariant variant = _pService ? _pService->GetChineseVariant() : IME_VARIANT_SIMPLIFIED;
    wcscpy_s(_tipVariant, variant == IME_VARIANT_TRADITIONAL ? M2C_TIP_TRADITIONAL : M2C_TIP_SIMPLIFIED);

    wcscpy_s(_tipSettings, M2C_TIP_SETTINGS);

    if (!_hwndTip) return;

    TOOLINFO ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd = _hwnd;

    ti.uId = 1;
    ti.lpszText = _tipMode;
    SendMessageW(_hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

    ti.uId = 2;
    ti.lpszText = _tipWidth;
    SendMessageW(_hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

    ti.uId = 3;
    ti.lpszText = _tipPunct;
    SendMessageW(_hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

    ti.uId = 4;
    ti.lpszText = _tipVariant;
    SendMessageW(_hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);

    ti.uId = 5;
    ti.lpszText = _tipSettings;
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

    int x = 0;
    int y = 0;
    if (_hasCustomPos)
    {
        // 使用用户拖动后的位置；显示器分辨率/布局变化后自动拉回可见区域
        x = _posX;
        y = _posY;
        _ClampToWorkArea(x, y);
        _posX = x;
        _posY = y;
    }
    else
    {
        // 默认停靠在主屏工作区右下角（任务栏之上）
        HMONITOR hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMon, &mi);

        x = mi.rcWork.right - kWidth - 6;
        y = mi.rcWork.bottom - kHeight - 6;
    }

    SetWindowPos(_hwnd, HWND_TOPMOST, x, y, kWidth, kHeight, SWP_NOACTIVATE);
}

// 把状态条限制在离它最近的显示器工作区内，避免拖到屏幕外后找不回来
void CStatusBar::_ClampToWorkArea(int& x, int& y) const
{
    RECT rc = { x, y, x + kWidth, y + kHeight };
    HMONITOR hMon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi))
        return;

    const RECT& work = mi.rcWork;
    if (x + kWidth > work.right)  x = work.right - kWidth;
    if (y + kHeight > work.bottom) y = work.bottom - kHeight;
    if (x < work.left)            x = work.left;
    if (y < work.top)             y = work.top;
}

void CStatusBar::_LoadSavedPosition()
{
    _hasCustomPos = false;

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type = 0, size = sizeof(DWORD);
        DWORD x = 0, y = 0;
        if (RegQueryValueExW(hKey, kRegPosX, nullptr, &type, (BYTE*)&x, &size) == ERROR_SUCCESS
            && type == REG_DWORD && size == sizeof(DWORD))
        {
            size = sizeof(DWORD);
            if (RegQueryValueExW(hKey, kRegPosY, nullptr, &type, (BYTE*)&y, &size) == ERROR_SUCCESS
                && type == REG_DWORD && size == sizeof(DWORD))
            {
                _posX = (int)(LONG)x;
                _posY = (int)(LONG)y;
                _hasCustomPos = true;
            }
        }
        RegCloseKey(hKey);
    }

    if (_hasCustomPos)
        _ClampToWorkArea(_posX, _posY);
}

void CStatusBar::_SavePosition(int x, int y)
{
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
    {
        DWORD v = (DWORD)x;
        RegSetValueExW(hKey, kRegPosX, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        v = (DWORD)y;
        RegSetValueExW(hKey, kRegPosY, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

void CStatusBar::_Show()
{
    _EnsureWindow();
    if (!_hwnd) return;
    if (!_dragging)
    {
        _LoadSavedPosition();   // 兼容其它应用里的状态条实例刚拖动过的情况
        _Reposition();
    }
    ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
    InvalidateRect(_hwnd, nullptr, FALSE);
}

void CStatusBar::_Hide()
{
    if (_dragging)
        _CancelDrag();
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
    if (_fontWidth) { DeleteObject(_fontWidth); _fontWidth = nullptr; }
    if (_fontVariant) { DeleteObject(_fontVariant); _fontVariant = nullptr; }
    if (_fontSettings) { DeleteObject(_fontSettings); _fontSettings = nullptr; }
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
    ImeWidthMode widthMode = _pService ? _pService->GetWidthMode() : IME_WIDTH_HALFWIDTH;
    ImeChineseVariant variant = _pService ? _pService->GetChineseVariant() : IME_VARIANT_SIMPLIFIED;
    LPCWSTR text = L"普";
    COLORREF color = kTextCn;
    switch (mode)
    {
    case IME_MODE_ENGLISH:   text = L"英"; color = kTextEn; break;
    case IME_MODE_MANDARIN:  text = L"普"; color = kTextCn; break;
    case IME_MODE_CANTONESE: text = (variant == IME_VARIANT_TRADITIONAL) ? L"粵" : L"粤"; color = kTextCn; break;
    }

    // 模式字
    SelectObject(memDC, _fontMode);
    SetTextColor(memDC, color);
    RECT rcText = rc;
    rcText.right = kModeRight;  // 模式字占左边 40px
    DrawTextW(memDC, text, 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 分隔线（模式 / 全角半角 / 标点 / 简繁 四段之间）
    HPEN sepPen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldSepPen = SelectObject(memDC, sepPen);
    MoveToEx(memDC, kModeRight, 4, nullptr);
    LineTo(memDC, kModeRight, rc.bottom - 5);
    MoveToEx(memDC, kWidthRight, 4, nullptr);
    LineTo(memDC, kWidthRight, rc.bottom - 5);
    MoveToEx(memDC, kPunctRight, 4, nullptr);
    LineTo(memDC, kPunctRight, rc.bottom - 5);
    MoveToEx(memDC, kVariantRight, 4, nullptr);
    LineTo(memDC, kVariantRight, rc.bottom - 5);
    MoveToEx(memDC, kSettingsRight, 4, nullptr);
    LineTo(memDC, kSettingsRight, rc.bottom - 5);
    SelectObject(memDC, oldSepPen);
    DeleteObject(sepPen);

    // 全角/半角状态字（▶ 全角 / ▷ 半角）
    LPCWSTR widthText = (widthMode == IME_WIDTH_FULLWIDTH) ? L"\u25B6" : L"\u25B7";
    COLORREF widthColor = (widthMode == IME_WIDTH_FULLWIDTH) ? kTextCn : kTextEn;
    SelectObject(memDC, _fontWidth);
    SetTextColor(memDC, widthColor);
    RECT rcWidth = rc;
    rcWidth.left = kModeRight;
    rcWidth.right = kWidthRight;
    DrawTextW(memDC, widthText, 1, &rcWidth, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 中文/英文标点状态符号（○ 中文标点 / ● 英文标点）
    LPCWSTR punctText = (punctMode == IME_PUNCT_FULLWIDTH) ? L"\u25CB" : L"\u25CF";
    COLORREF punctColor = (punctMode == IME_PUNCT_FULLWIDTH) ? kTextCn : kTextEn;
    SelectObject(memDC, _fontPunct);
    SetTextColor(memDC, punctColor);
    RECT rcPunct = rc;
    rcPunct.left = kWidthRight;
    rcPunct.right = kPunctRight;
    DrawTextW(memDC, punctText, 1, &rcPunct, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 简体/繁体状态字（简 / 繁）
    LPCWSTR variantText = (variant == IME_VARIANT_TRADITIONAL) ? L"繁" : L"简";
    COLORREF variantColor = (variant == IME_VARIANT_TRADITIONAL) ? kTextCn : kTextEn;
    SelectObject(memDC, _fontVariant);
    SetTextColor(memDC, variantColor);
    RECT rcVariant = rc;
    rcVariant.left = kPunctRight;
    rcVariant.right = kVariantRight;
    DrawTextW(memDC, variantText, 1, &rcVariant, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 设置齿轮（⚙）
    SelectObject(memDC, _fontSettings);
    SetTextColor(memDC, kTextEn);
    RECT rcSettings = rc;
    rcSettings.left = kVariantRight;
    rcSettings.right = kSettingsRight;
    DrawTextW(memDC, L"\u2699", 1, &rcSettings, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // ▾
    SelectObject(memDC, _fontArrow);
    SetTextColor(memDC, kArrow);
    RECT rcArrow = rc;
    rcArrow.left = kSettingsRight;
    rcArrow.right = rc.right - 4;
    DrawTextW(memDC, L"▼", 1, &rcArrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(_hwnd, &ps);
    return 0;
}

void CStatusBar::_OnLButtonDown(LPARAM lParam)
{
    if (!_hwnd) return;

    _dragging = true;
    _dragMoved = false;
    _suppressClick = false;
    GetCursorPos(&_dragStart);

    RECT rc = {};
    GetWindowRect(_hwnd, &rc);
    _dragOrigin.x = rc.left;
    _dragOrigin.y = rc.top;

    SetCapture(_hwnd);
    SetTimer(_hwnd, kDragTimerId, kDragTimerMs, nullptr);

    // 拖动时先收起分区提示，避免提示窗跟着乱跑
    if (_hwndTip)
        SendMessageW(_hwndTip, TTM_POP, 0, 0);

    UNREFERENCED_PARAMETER(lParam);
}

// SetCapture 对 WS_EX_NOACTIVATE 窗口不一定能持续收到鼠标消息，
// 用定时器轮询光标位置和按键状态，保证拖动始终可用。
void CStatusBar::_OnDragTimer()
{
    if (!_hwnd) return;
    if (!_dragging)
    {
        KillTimer(_hwnd, kDragTimerId);
        return;
    }

    POINT pt = {};
    GetCursorPos(&pt);
    int dx = pt.x - _dragStart.x;
    int dy = pt.y - _dragStart.y;

    int cxDrag = GetSystemMetrics(SM_CXDRAG);
    int cyDrag = GetSystemMetrics(SM_CYDRAG);
    if (!_dragMoved && (dx <= -cxDrag || dx >= cxDrag ||
                        dy <= -cyDrag || dy >= cyDrag))
    {
        _dragMoved = true;
    }

    if (_dragMoved)
    {
        SetWindowPos(_hwnd, HWND_TOPMOST,
                     _dragOrigin.x + dx, _dragOrigin.y + dy,
                     0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
    }

    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
    {
        bool moved = _dragMoved;
        _dragging = false;
        _dragMoved = false;
        KillTimer(_hwnd, kDragTimerId);
        if (GetCapture() == _hwnd)
            ReleaseCapture();
        if (moved)
            _FinishDrag(true);
        // 未移动时保留点击动作，交给随后到达的 WM_LBUTTONUP 处理
    }
}

void CStatusBar::_FinishDrag(bool suppressClick)
{
    if (!_hwnd) return;

    // 拖动结束：吸附到最近的显示器工作区并持久化位置
    RECT rc = {};
    if (GetWindowRect(_hwnd, &rc))
    {
        int x = rc.left;
        int y = rc.top;
        _ClampToWorkArea(x, y);
        if (x != rc.left || y != rc.top)
            SetWindowPos(_hwnd, HWND_TOPMOST, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);

        _posX = x;
        _posY = y;
        _hasCustomPos = true;
        _SavePosition(x, y);
    }

    if (suppressClick)
        _suppressClick = true;
    InvalidateRect(_hwnd, nullptr, FALSE);
}

void CStatusBar::_CancelDrag()
{
    _dragging = false;
    _dragMoved = false;
    _suppressClick = false;
    if (_hwnd)
    {
        KillTimer(_hwnd, kDragTimerId);
        if (GetCapture() == _hwnd)
            ReleaseCapture();
    }
}

void CStatusBar::_OnMouseMove(WPARAM wParam, LPARAM lParam)
{
    if (_dragging)
    {
        POINT pt = {};
        GetCursorPos(&pt);
        int dx = pt.x - _dragStart.x;
        int dy = pt.y - _dragStart.y;

        // 超过系统拖动阈值才算拖动，否则松手仍按普通点击处理
        int cxDrag = GetSystemMetrics(SM_CXDRAG);
        int cyDrag = GetSystemMetrics(SM_CYDRAG);
        if (!_dragMoved && (dx <= -cxDrag || dx >= cxDrag ||
                            dy <= -cyDrag || dy >= cyDrag))
        {
            _dragMoved = true;
        }

        if (_dragMoved)
        {
            // 拖动过程中不裁剪，方便跨显示器移动；松手时再拉回工作区
            SetWindowPos(_hwnd, HWND_TOPMOST,
                         _dragOrigin.x + dx, _dragOrigin.y + dy,
                         0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
        }
        return;
    }

    if (!_hover)
    {
        _hover = true;
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = _hwnd;
        TrackMouseEvent(&tme);
        InvalidateRect(_hwnd, nullptr, FALSE);
    }
    // 转发鼠标移动，让提示窗口按所在区域弹出“模式/标点”提示
    _RelayMouseToTooltip(WM_MOUSEMOVE, wParam, lParam);
}

void CStatusBar::_OnLButtonUp(LPARAM lParam)
{
    if (_suppressClick)
    {
        // 拖动已由定时器结束，这次 WM_LBUTTONUP 不能再触发分区动作
        _suppressClick = false;
        _dragging = false;
        _dragMoved = false;
        if (_hwnd)
            KillTimer(_hwnd, kDragTimerId);
        return;
    }

    bool wasDragging = _dragging;
    bool moved = _dragMoved;
    _dragging = false;
    _dragMoved = false;
    if (_hwnd)
        KillTimer(_hwnd, kDragTimerId);

    if (wasDragging)
        ReleaseCapture();

    if (moved)
    {
        _FinishDrag(false);
        return;   // 拖动后不再触发分区点击动作
    }

    if (!_pService) return;

    // 获取点击位置（窗口客户区坐标）
    int clickX = GET_X_LPARAM(lParam);

    // 模式区（0 ~ kModeRight）：弹出模式菜单
    // 全角/半角区（kModeRight ~ kWidthRight）：切换全角/半角
    // 标点区（kWidthRight ~ kPunctRight）：切换中文/英文标点
    // 简/繁区（kPunctRight ~ kVariantRight）：切换简体/繁体
    // 设置区（kVariantRight ~ kSettingsRight）：打开设置对话框
    if (clickX >= kModeRight && clickX < kWidthRight)
    {
        _pService->ToggleWidthMode();
        _Update();
        return;
    }
    if (clickX >= kWidthRight && clickX < kPunctRight)
    {
        _pService->TogglePunctMode();
        _Update();
        return;
    }
    if (clickX >= kPunctRight && clickX < kVariantRight)
    {
        _pService->ToggleChineseVariant();
        _Update();
        return;
    }
    if (clickX >= kVariantRight && clickX < kSettingsRight)
    {
        // 打开"设置"对话框；本窗口为置顶且不激活窗口，用其作为 owner 保证对话框显示在最前
        M2CShowSettingsDialog(_pService, _hwnd);
        return;
    }
    if (clickX >= kSettingsRight)
    {
        // 箭头区：也弹出模式菜单
    }
    else
    {
        // 模式区
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
    case WM_LBUTTONDOWN:
        pThis->_OnLButtonDown(lParam);
        return 0;
    case WM_LBUTTONUP:
        pThis->_OnLButtonUp(lParam);
        return 0;
    case WM_MOUSEMOVE:
        pThis->_OnMouseMove(wParam, lParam);
        return 0;
    case WM_TIMER:
        if (wParam == kDragTimerId)
        {
            pThis->_OnDragTimer();
            return 0;
        }
        break;
    case WM_CANCELMODE:
        if (pThis->_dragging)
            pThis->_CancelDrag();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursor(nullptr,
                                 (pThis->_dragging && pThis->_dragMoved) ? IDC_SIZEALL : IDC_HAND));
            return TRUE;
        }
        break;
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

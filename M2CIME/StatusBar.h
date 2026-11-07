#pragma once

// CStatusBar —— 输入法自带的状态条（常驻"语言栏"，点击弹出 英/普/粤 菜单）
// 扩展：同时显示中文/英文标点状态，点击切换
#include "Globals.h"

class CTextService;

class CStatusBar
{
public:
    CStatusBar(CTextService* pService);
    ~CStatusBar();

    void _Show();
    void _Hide();
    void _Update();          // 模式变化后刷新显示
    void _Destroy();

private:
    static LRESULT CALLBACK _WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT _OnPaint();
    void _OnLButtonUp(LPARAM lParam);  // lParam 含鼠标坐标
    void _EnsureWindow();
    void _Reposition();
    void _EnsureTooltip();
    void _UpdateTooltipText();
    void _RelayMouseToTooltip(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    CTextService* _pService;
    HWND _hwnd;
    HWND _hwndTip;      // 悬停提示窗口
    HFONT _fontMode;
    HFONT _fontArrow;
    HFONT _fontPunct;
    HFONT _fontWidth;
    bool _hover;
    WCHAR _tipMode[16];   // 模式区悬停提示（当前模式名）
    WCHAR _tipPunct[16];  // 中文/英文标点区悬停提示（当前状态）
    WCHAR _tipWidth[16];  // 全角/半角区悬停提示（当前状态）

    static const int kWidth  = 144;  // 模式 + 标点 + 全角/半角 + 箭头
    static const int kHeight = 34;
    static const int kModeRight  = 40;  // 模式区右边界（分隔线左侧）
    static const int kPunctRight = 79;  // 标点区右边界（分隔线左侧）
    static const int kWidthSegRight = 118; // 宽度区右边界（分隔线左侧）
};

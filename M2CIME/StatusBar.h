#pragma once

// CStatusBar —— 输入法自带的状态条（常驻"语言栏"，点击弹出 英/普/粤 菜单）
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
    void _OnLButtonUp();
    void _EnsureWindow();
    void _Reposition();

private:
    CTextService* _pService;
    HWND _hwnd;
    HFONT _fontMode;
    HFONT _fontArrow;
    bool _hover;

    static const int kWidth  = 104;
    static const int kHeight = 34;
};

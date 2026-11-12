#pragma once

// CStatusBar —— 输入法自带的状态条（常驻"语言栏"，点击弹出 英/普/粤 菜单）
// 扩展：同时显示中文/英文标点、全角/半角、简体/繁体状态，点击对应区域切换
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
    void _OnLButtonDown(LPARAM lParam);  // 开始可能的拖动
    void _OnLButtonUp(LPARAM lParam);    // 拖动结束或执行点击动作
    void _OnMouseMove(WPARAM wParam, LPARAM lParam);
    void _OnDragTimer();                 // 轮询鼠标位置，兼容 SetCapture 失效的情况
    void _FinishDrag(bool suppressClick); // 保存拖动结果
    void _CancelDrag();                  // 放弃本次拖动（窗口隐藏等）
    void _EnsureWindow();
    void _Reposition();
    void _ClampToWorkArea(int& x, int& y) const;  // 保证状态条完整落在显示器工作区内
    void _LoadSavedPosition();                    // 读取用户拖动后保存的位置
    void _SavePosition(int x, int y);
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
    HFONT _fontVariant;
    HFONT _fontSettings;
    bool _hover;

    // 拖动移动状态条
    bool _dragging;        // 左键按下后进入拖动跟踪
    bool _dragMoved;       // 移动超过系统拖动阈值，本次不再当作点击
    bool _suppressClick;   // 拖动已由定时器结束，忽略随后到达的 WM_LBUTTONUP
    POINT _dragStart;      // 按下时的屏幕坐标
    POINT _dragOrigin;     // 按下时的窗口左上角（屏幕坐标）
    bool _hasCustomPos;    // 是否已有用户拖动保存的位置
    int _posX;
    int _posY;

    WCHAR _tipMode[64];    // 模式区悬停提示（当前模式名 + 快捷键）
    WCHAR _tipPunct[16];   // 中文/英文标点区悬停提示（当前状态）
    WCHAR _tipWidth[64];   // 全角/半角区悬停提示（当前状态 + 快捷键）
    WCHAR _tipVariant[16]; // 简体/繁体区悬停提示（当前状态）
    WCHAR _tipSettings[16];// 设置区悬停提示

    static const int kWidth  = 221;  // 模式 + 全角/半角 + 标点 + 简/繁 + 设置 + 箭头
    static const int kHeight = 34;
    static const int kModeRight     = 40;   // 模式区右边界（分隔线位置）
    static const int kWidthRight    = 79;   // 全角/半角区右边界
    static const int kPunctRight    = 118;  // 中文/英文标点区右边界
    static const int kVariantRight  = 157;  // 简体/繁体区右边界
    static const int kSettingsRight = 191;  // 设置区右边界（右侧为箭头）
};

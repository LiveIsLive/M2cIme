#pragma once

// CCandidateWindow —— 自绘候选词窗口（竖排、跟随光标、鼠标可选词）
#include "Globals.h"

class CTextService;

class CCandidateWindow
{
public:
    CCandidateWindow(CTextService* pService);
    ~CCandidateWindow();

    // 简单引用计数（TextService 持有）
    ULONG AddRef()  { return InterlockedIncrement(&_cRef); }
    ULONG Release()
    {
        LONG c = InterlockedDecrement(&_cRef);
        if (c == 0) delete this;
        return c;
    }

    // 刷新内容与位置（items: <候选词, 来源0普/1粤>，caretRect 为屏幕坐标，可为空）
    // hwndOwner 为当前输入窗口：候选窗必须是它的属主窗口，否则鼠标点击选词时
    // 焦点会落到候选窗上，目标程序无法继续输入（连续选词尤其明显）。
    void _Update(const std::wstring& pinyin,
                 const std::vector<std::pair<std::wstring, int>>& items,
                 int pageIndex, int totalPages,
                 const RECT* caretRect,
                 HWND hwndOwner = nullptr);
    void _Hide();
    void _Destroy();

    bool IsVisible() const { return _hwnd != nullptr && IsWindowVisible(_hwnd); }
    HWND GetWnd() const { return _hwnd; }

private:
    static LRESULT CALLBACK _WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT _OnPaint();
    LRESULT _OnMouseMove(int x, int y);
    LRESULT _OnLButtonDown(int x, int y);
    LRESULT _OnLButtonUp(int x, int y);

    void _EnsureWindow();
    void _RecalcLayout();     // 计算各区域矩形与窗口尺寸
    int  _HitTest(int y) const; // 返回候选项索引，-1 表示不在候选项上
    const WCHAR* _SourceTag() const; // 粤语来源标记（简/繁自适应）

private:
    CTextService* _pService;
    LONG _cRef;
    HWND _hwnd;
    HWND _hwndOwner;            // 属主窗口（当前有焦点的输入控件）

    std::wstring _pinyin;
    std::vector<std::pair<std::wstring, int>> _items;
    int _pageIndex;
    int _totalPages;
    int _hoverIndex;

    RECT _rcPinyin;
    RECT _rcItems[M2C_PAGE_SIZE];
    RECT _rcPageInfo;

    HFONT _fontPinyin;
    HFONT _fontItem;

    static const int kPadding = 10;
    static const int kPinyinHeight = 26;
    static const int kItemHeight = 30;
    static const int kPageInfoHeight = 22;
    static const int kMinWidth = 150;
    static const int kMaxWidth = 480;
};

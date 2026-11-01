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
    void _Update(const std::wstring& pinyin,
                 const std::vector<std::pair<std::wstring, int>>& items,
                 int pageIndex, int totalPages,
                 const RECT* caretRect);
    void _Hide();
    void _Destroy();

    bool IsVisible() const { return _hwnd != nullptr && IsWindowVisible(_hwnd); }

private:
    static LRESULT CALLBACK _WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT _OnPaint();
    LRESULT _OnMouseMove(int x, int y);
    LRESULT _OnLButtonUp(int x, int y);

    void _EnsureWindow();
    void _RecalcLayout();     // 计算各区域矩形与窗口尺寸
    int  _HitTest(int y) const; // 返回候选项索引，-1 表示不在候选项上

private:
    CTextService* _pService;
    LONG _cRef;
    HWND _hwnd;

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

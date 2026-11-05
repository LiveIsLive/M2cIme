#pragma once

// CTextService —— TSF 文本服务主类
// 实现：ITfTextInputProcessorEx / ITfKeyEventSink / ITfThreadFocusSink / ITfActiveLanguageProfileNotifySink

#include "Globals.h"

class CCandidateWindow;
class CLangBarButton;
class CLangBarPunctButton;
class CStatusBar;

class CTextService :
    public ITfTextInputProcessorEx,
    public ITfKeyEventSink,
    public ITfThreadFocusSink,
    public ITfActiveLanguageProfileNotifySink
{
public:
    CTextService();
    virtual ~CTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tfClientId, DWORD dwFlags) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pContext, REFGUID rguid, BOOL* pfEaten) override;

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus() override;
    STDMETHODIMP OnKillThreadFocus() override;

    // ITfActiveLanguageProfileNotifySink
    STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated) override;

    // ===== 内部方法 =====
    void  SetMode(ImeInputMode mode, bool fSaveToRegistry = true);
    ImeInputMode GetMode() const { return _mode; }
    ITfThreadMgr* GetThreadMgr() const { return _pThreadMgr; }

    // 全角/半角标点切换
    void  SetPunctMode(ImePunctMode mode, bool fSaveToRegistry = true);
    ImePunctMode GetPunctMode() const { return _punctMode; }
    void  TogglePunctMode();

    // 候选窗鼠标点击选词回调
    void OnCandidateClicked(int itemIndexOnPage);

private:
    // 按键处理（KeyHandler.cpp）
    BOOL _IsKeyEaten(WPARAM wParam, bool fTest);
    void _HandleKeyInSession(TfEditCookie ec, ITfContext* pContext, WPARAM wParam);

    // 提交引擎文本到文档（须在 edit session 中）
    HRESULT _InsertTextAtSelection(TfEditCookie ec, ITfContext* pContext, const WCHAR* text, LONG len);

    // 获取光标屏幕矩形（须在 edit session 中，需要至少 read 权限）
    BOOL _GetCaretRect(TfEditCookie ec, ITfContext* pContext, RECT* prc);

    // 候选窗刷新（在 edit session 外调用）
    void _UpdateCandidateWindow(ITfContext* pContext, const RECT* pCaretRect);

    // 标点映射：返回 TRUE 时 *pCn 为中文标点、*pEn 为英文标点
    static BOOL _MapPunct(WPARAM vk, WCHAR* pCn, WCHAR* pEn);

    // 结束标点候选状态
    void _ClearPunctPending();

    // 在标点序列末尾追加 / 移除一个标点
    void _AppendPunct(WCHAR cn, WCHAR en);
    void _RemoveLastPunct();

    // 模式持久化
    void _LoadModeFromRegistry();
    void _LoadPunctModeFromRegistry();

    // 语言栏按钮文字刷新
    void _UpdateLangBarButton();
    void _UpdateLangBarPunctButton();

    // 当前线程的活动输入法是否为本输入法
    BOOL _IsM2CProfileActive();

    // 按「是否有焦点 + 是否为当前输入法」同步状态条显隐
    void _SyncStatusBarVisibility();

private:
    LONG            _cRef;
    ITfThreadMgr*   _pThreadMgr;
    TfClientId      _tfClientId;
    DWORD           _dwThreadFocusSinkCookie;
    DWORD           _dwActiveProfileSinkCookie;

    CCandidateWindow* _pCandidateWnd;
    CLangBarButton*   _pLangBarButton;
    CLangBarPunctButton* _pLangBarPunctButton;  // 全角/半角标点切换按钮
    CStatusBar*       _pStatusBar;

    ImeInputMode    _mode;              // 当前模式
    ImeInputMode    _lastChineseMode;   // 最近的中文模式（普通话/粤语），Shift 切回时用
    ImePunctMode    _punctMode;         // 当前标点模式（全角/半角）
    bool            _shiftPressed;      // Shift 按下后尚未消费其它键
    bool            _keyHandledInSession; // edit session 中是否消费了按键
    int             _pageIndex;         // 候选翻页页号

    RECT            _caretRect;         // 最近一次 edit session 取到的光标矩形（屏幕坐标）
    bool            _hasCaretRect;

    bool            _punctPending;      // 标点候选状态：等待用户选择中文/英文标点
    bool            _punctInComp;       // 标点挂在拼音组合串末尾（选词后一起上屏）
    WCHAR           _punctCn[16];       // 标点序列：中文形式（如连续两个点为 。。 ）
    WCHAR           _punctEn[16];       // 标点序列：英文/原始形式（如 .. ）
    int             _punctLen;          // 标点序列长度
};

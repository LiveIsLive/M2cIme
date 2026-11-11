#pragma once

// CTextService —— TSF 文本服务主类
// 实现：ITfTextInputProcessorEx / ITfKeyEventSink / ITfThreadFocusSink /
//       ITfActiveLanguageProfileNotifySink / ITfCompartmentEventSink

#include "Globals.h"

enum PunctSide { PSIDE_NONE = 0, PSIDE_BEFORE = 1, PSIDE_AFTER = 2 };

class CCandidateWindow;
class CLangBarButton;
class CLangBarPunctButton;
class CLangBarWidthButton;
class CLangBarVariantButton;
class CLangBarSettingsButton;
class CStatusBar;

class CTextService :
    public ITfTextInputProcessorEx,
    public ITfKeyEventSink,
    public ITfThreadFocusSink,
    public ITfActiveLanguageProfileNotifySink,
    public ITfCompartmentEventSink
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

    // ITfCompartmentEventSink（键盘开/关舱室）
    STDMETHODIMP OnChange(REFGUID rguid) override;

    // ===== 内部方法 =====
    void  SetMode(ImeInputMode mode, bool fSaveToRegistry = true);
    ImeInputMode GetMode() const { return _mode; }
    ITfThreadMgr* GetThreadMgr() const { return _pThreadMgr; }

    // 中文/英文标点切换
    void  SetPunctMode(ImePunctMode mode, bool fSaveToRegistry = true);
    ImePunctMode GetPunctMode() const { return _punctMode; }
    void  TogglePunctMode();

    // 全角/半角切换（与标点模式相互独立）
    void  SetWidthMode(ImeWidthMode mode, bool fSaveToRegistry = true);
    ImeWidthMode GetWidthMode() const { return _widthMode; }
    void  ToggleWidthMode();

    // 简体/繁体切换（只影响汉字候选与上屏文本）
    void  SetChineseVariant(ImeChineseVariant variant, bool fSaveToRegistry = true);
    ImeChineseVariant GetChineseVariant() const { return _chineseVariant; }
    void  ToggleChineseVariant();

    // 拼音输入方式（全拼/双拼）
    void  SetPinyinMode(ImePinyinMode mode, bool fSaveToRegistry = true);
    ImePinyinMode GetPinyinMode() const { return _pinyinMode; }

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

    // 前标点 / 后标点序列追加 / 移除
    void _AppendPunctBefore(WCHAR cn, WCHAR en);
    void _AppendPunctAfter(WCHAR cn, WCHAR en);
    void _RemoveLastPunct();

    // 在混合态决定展示哪个标点的候选（优先前标点）
    void _GetMixedPunctSource(WCHAR* pCn, WCHAR* pEn, PunctSide* pSide) const;

    // 模式持久化
    void _LoadModeFromRegistry();
    void _LoadPunctModeFromRegistry();
    void _LoadWidthModeFromRegistry();
    void _LoadChineseVariantFromRegistry();
    void _LoadPinyinModeFromRegistry();

    // 语言栏按钮文字刷新
    void _UpdateLangBarButton();
    void _UpdateLangBarPunctButton();
    void _UpdateLangBarWidthButton();
    void _UpdateLangBarVariantButton();

    // 当前线程的活动输入法是否为本输入法
    BOOL _IsM2CProfileActive();

    // 按「是否有焦点 + 是否为当前输入法」同步状态条显隐
    void _SyncStatusBarVisibility();

    // 键盘开/关舱室（GUID_COMPARTMENT_KEYBOARD_OPENCLOSE）联动：
    // 系统输入法热键 Ctrl+空格 由 imm32/CUAS 层处理并翻转该舱室（微软拼音同款机制），
    // M2C 依此切换内部中英文模式；SetMode 时反向同步，保持状态一致
    void _SyncKeyboardOpenClose(ImeInputMode mode);
    LONG _ReadCompartmentLong(REFGUID rguid);

private:
    LONG            _cRef;
    ITfThreadMgr*   _pThreadMgr;
    TfClientId      _tfClientId;
    DWORD           _dwThreadFocusSinkCookie;
    DWORD           _dwActiveProfileSinkCookie;
    DWORD           _dwCompartmentSinkCookie;   // 键盘开/关舱室 sink（OPENCLOSE）
    bool            _fInCompartmentSink;        // 正在处理舱室事件（防止回环）

    CCandidateWindow* _pCandidateWnd;
    CLangBarButton*   _pLangBarButton;
    CLangBarPunctButton* _pLangBarPunctButton;  // 中文/英文标点切换按钮
    CLangBarWidthButton* _pLangBarWidthButton;  // 全角/半角切换按钮
    CLangBarVariantButton* _pLangBarVariantButton; // 简体/繁体切换按钮
    CLangBarSettingsButton* _pLangBarSettingsButton; // 设置按钮
    CStatusBar*       _pStatusBar;

    ImeInputMode    _mode;              // 当前模式
    ImeInputMode    _lastChineseMode;   // 最近的中文模式（普通话/粤语），Shift 切回时用
    ImePunctMode    _punctMode;         // 当前标点模式（中文/英文标点）
    ImeWidthMode    _widthMode;         // 当前字符宽度模式（全角/半角）
    ImeChineseVariant _chineseVariant;  // 当前中文字形（简体/繁体）
    ImePinyinMode   _pinyinMode;        // 当前拼音输入方式（全拼/双拼）
    bool            _shiftPressed;      // Shift 按下后尚未消费其它键
    bool            _keyHandledInSession; // edit session 中是否消费了按键
    int             _pageIndex;         // 候选翻页页号

    RECT            _caretRect;         // 最近一次 edit session 取到的光标矩形（屏幕坐标）
    bool            _hasCaretRect;

    bool            _punctPending;      // 标点状态激活（候选中或挂在组合串上）
    bool            _punctInComp;       // 标点已挂到组合串上（选词后一起上屏）
    WCHAR           _punctBeforeCn[16]; // 拼音前标点：中文形式（如引号前半）
    WCHAR           _punctBeforeEn[16]; // 拼音前标点：英文/原始形式
    int             _punctBeforeLen;    // 前标点序列长度
    WCHAR           _punctAfterCn[16];  // 拼音后标点：中文形式（如引号后半）
    WCHAR           _punctAfterEn[16];  // 拼音后标点：英文/原始形式
    int             _punctAfterLen;     // 后标点序列长度
    int             _punctAfterPos;     // 后标点在组合串中的插入位置（输入标点时的组合串长度）

    int             _mixedPunctCandCount; // 混合态候选列表里前几个"标点自身候选"的数量
};

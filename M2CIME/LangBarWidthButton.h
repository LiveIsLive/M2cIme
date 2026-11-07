#pragma once

// CLangBarWidthButton —— 语言栏全角/半角切换按钮
#include "Globals.h"

class CTextService;

class CLangBarWidthButton :
    public ITfLangBarItemButton,
    public ITfSource
{
public:
    CLangBarWidthButton(CTextService* pService);
    virtual ~CLangBarWidthButton();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo) override;
    STDMETHODIMP GetStatus(DWORD* pdwStatus) override;
    STDMETHODIMP Show(BOOL fShow) override;
    STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea) override;
    STDMETHODIMP InitMenu(ITfMenu* pMenu) override;
    STDMETHODIMP OnMenuSelect(UINT wID) override;
    STDMETHODIMP GetIcon(HICON* phIcon) override;
    STDMETHODIMP GetText(BSTR* pbstrText) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) override;
    STDMETHODIMP UnadviseSink(DWORD dwCookie) override;

    // 状态变化后刷新按钮图标/文字
    void _NotifyUpdate();

private:
    // 取当前宽度模式对应的图标（懒创建）
    HICON _GetWidthModeIcon();
    // 用 GDI 画一个"圆角底 + 白色字"的小图标
    static HICON _CreateWidthModeIcon(const WCHAR* text, COLORREF bg);

private:
    LONG _cRef;
    CTextService* _pService;
    ITfLangBarItemSink* _pSink;
    DWORD _dwSinkCookie;
    HICON _hIconFull;   // 全角图标（全）
    HICON _hIconHalf;   // 半角图标（半）
};

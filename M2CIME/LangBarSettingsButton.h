#pragma once

// CLangBarSettingsButton —— 语言栏设置按钮（点击打开设置对话框）
#include "Globals.h"

class CTextService;

class CLangBarSettingsButton :
    public ITfLangBarItemButton,
    public ITfSource
{
public:
    CLangBarSettingsButton(CTextService* pService);
    virtual ~CLangBarSettingsButton();

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

private:
    HICON _GetSettingsIcon();
    static HICON _CreateSettingsIcon(const WCHAR* text, COLORREF bg);

private:
    LONG _cRef;
    CTextService* _pService;
    ITfLangBarItemSink* _pSink;
    DWORD _dwSinkCookie;
    HICON _hIcon;
};

// LangBarSettingsButton.cpp —— 语言栏设置按钮（点击打开设置对话框）
#include "Globals.h"
#include "LangBarSettingsButton.h"
#include "TextService.h"
#include "resource.h"
#include <windowsx.h>   // ComboBox_* 宏
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

#include "../MandarinToCantonese/Dictionary.h"

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")

// 设置字符：齿轮（U+2699）
#define M2C_SETTINGS_GLYPH  L"\u2699"

namespace
{
    struct DictMgrState
    {
        std::vector<MandarinLibraryInfo> libs;
    };

    void RefreshDictList(HWND hDlg, DictMgrState* st)
    {
        HWND hList = GetDlgItem(hDlg, IDC_DICT_LIST);
        if (!hList || !st) return;
        int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
        long long selId = -1;
        if (sel >= 0 && sel < (int)st->libs.size()) selId = st->libs[sel].id;

        SendMessageW(hList, LB_RESETCONTENT, 0, 0);
        st->libs = Dictionary::Instance().GetMandarinLibraries();

        int newSel = -1;
        for (size_t i = 0; i < st->libs.size(); ++i)
        {
            const auto& lib = st->libs[i];
            std::wstring text = lib.name + L"  (" + std::to_wstring(lib.wordCount) + L" 个词)";
            int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)text.c_str());
            if (lib.id == selId) newSel = idx;
            (void)idx;
        }
        if (newSel < 0 && !st->libs.empty())
        {
            if (sel >= (int)st->libs.size()) newSel = (int)st->libs.size() - 1;
            else if (sel >= 0) newSel = sel;
            else newSel = 0;
        }
        if (newSel >= 0) SendMessageW(hList, LB_SETCURSEL, newSel, 0);
    }

    // ===== 词库查看对话框 =====

    struct DictViewState
    {
        std::vector<LibraryWordEntry> words;
    };

    INT_PTR CALLBACK DictViewDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            INITCOMMONCONTROLSEX icc = {};
            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_LISTVIEW_CLASSES;
            InitCommonControlsEx(&icc);

            auto* st = new DictViewState();
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)st);

            const auto* lib = reinterpret_cast<const MandarinLibraryInfo*>(lParam);
            if (lib)
            {
                std::wstring title = L"查看词库 - " + lib->name;
                SetWindowTextW(hDlg, title.c_str());
                st->words = Dictionary::Instance().GetLibraryWords(lib->id);
                std::wstring info = lib->name + L"  （共 " +
                                    std::to_wstring(st->words.size()) + L" 个字词）";
                SetDlgItemTextW(hDlg, IDC_DICT_VIEW_INFO, info.c_str());
            }

            HWND hList = GetDlgItem(hDlg, IDC_DICT_VIEW_LIST);
            if (hList)
            {
                ListView_SetExtendedListViewStyle(hList,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
                LVCOLUMNW col = {};
                col.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
                col.cx = 200;
                col.pszText = const_cast<wchar_t*>(L"字词");
                ListView_InsertColumn(hList, 0, &col);
                col.pszText = const_cast<wchar_t*>(L"拼音");
                ListView_InsertColumn(hList, 1, &col);
                // 词库可能有数万条；LVS_OWNERDATA 只在需要绘制时取数据，避免逐条插入卡顿
                ListView_SetItemCount(hList, (int)st->words.size());
            }
            return TRUE;
        }
        case WM_NOTIFY:
        {
            const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
            if (nm && nm->idFrom == IDC_DICT_VIEW_LIST && nm->code == LVN_GETDISPINFOW)
            {
                auto* st = reinterpret_cast<DictViewState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
                auto* di = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                if (st && (di->item.mask & LVIF_TEXT) && di->item.pszText &&
                    di->item.cchTextMax > 0 && di->item.iItem >= 0 &&
                    di->item.iItem < (int)st->words.size())
                {
                    const auto& entry = st->words[(size_t)di->item.iItem];
                    const std::wstring& text =
                        (di->item.iSubItem == 0) ? entry.word : entry.pinyin;
                    const size_t n = (std::min)(text.size(), (size_t)di->item.cchTextMax - 1);
                    CopyMemory(di->item.pszText, text.data(), n * sizeof(wchar_t));
                    di->item.pszText[n] = L'\0';
                }
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        case WM_DESTROY:
        {
            auto* st = reinterpret_cast<DictViewState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            if (st) { delete st; SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0); }
            return TRUE;
        }
        }
        return FALSE;
    }

    void ShowDictViewDialog(HWND hwndOwner, const MandarinLibraryInfo& lib)
    {
        DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_DICT_VIEW), hwndOwner,
                        DictViewDlgProc, (LPARAM)&lib);
    }

    INT_PTR CALLBACK DictMgrDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            auto* st = new DictMgrState();
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)st);
            RefreshDictList(hDlg, st);
            return TRUE;
        }
        case WM_COMMAND:
        {
            auto* st = reinterpret_cast<DictMgrState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            const WORD id = LOWORD(wParam);
            HWND hList = GetDlgItem(hDlg, IDC_DICT_LIST);
            int sel = hList ? (int)SendMessageW(hList, LB_GETCURSEL, 0, 0) : -1;

            if (id == IDC_DICT_MOVE_UP || id == IDC_DICT_MOVE_DOWN)
            {
                if (st && sel >= 0 && sel < (int)st->libs.size())
                {
                    Dictionary::Instance().MoveMandarinLibrary(st->libs[sel].id, id == IDC_DICT_MOVE_UP ? -1 : 1);
                    RefreshDictList(hDlg, st);
                }
                return TRUE;
            }
            if (id == IDC_DICT_IMPORT)
            {
                wchar_t file[MAX_PATH] = {};
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hDlg;
                ofn.lpstrFilter = L"QQ拼音词库 (*.qcel)\0*.qcel\0所有文件 (*.*)\0*.*\0";
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
                ofn.lpstrDefExt = L"qcel";
                if (GetOpenFileNameW(&ofn))
                {
                    std::wstring name, err;
                    if (!Dictionary::Instance().ImportQcelLibrary(file, name, err))
                    {
                        if (err.empty()) err = L"导入失败";
                        MessageBoxW(hDlg, err.c_str(), L"导入词库", MB_OK | MB_ICONWARNING);
                    }
                    else
                    {
                        std::wstring msg = L"已导入词库：" + name;
                        MessageBoxW(hDlg, msg.c_str(), L"导入词库", MB_OK | MB_ICONINFORMATION);
                        RefreshDictList(hDlg, st);
                    }
                }
                return TRUE;
            }
            if (id == IDC_DICT_DELETE)
            {
                if (st && sel >= 0 && sel < (int)st->libs.size())
                {
                    if (st->libs.size() <= 1)
                    {
                        MessageBoxW(hDlg, L"至少要保留一个普通话词库，不能删除最后一个词库。", L"删除词库", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    std::wstring prompt = L"确定要删除词库“" + st->libs[sel].name + L"”吗？";
                    if (MessageBoxW(hDlg, prompt.c_str(), L"删除词库", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
                    {
                        std::wstring err;
                        if (!Dictionary::Instance().DeleteMandarinLibrary(st->libs[sel].id, err))
                        {
                            if (err.empty()) err = L"删除失败";
                            MessageBoxW(hDlg, err.c_str(), L"删除词库", MB_OK | MB_ICONWARNING);
                        }
                        RefreshDictList(hDlg, st);
                    }
                }
                return TRUE;
            }
            if (id == IDC_DICT_DOWNLOAD)
            {
                ShellExecuteW(hDlg, L"open", L"https://cdict.qq.pinyin.cn/", nullptr, nullptr, SW_SHOWNORMAL);
                return TRUE;
            }
            if (id == IDC_DICT_VIEW)
            {
                if (st && sel >= 0 && sel < (int)st->libs.size())
                    ShowDictViewDialog(hDlg, st->libs[sel]);
                return TRUE;
            }
            if (id == IDCANCEL)
            {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        case WM_DESTROY:
        {
            auto* st = reinterpret_cast<DictMgrState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            if (st) { delete st; SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0); }
            return TRUE;
        }
        }
        return FALSE;
    }

    void ShowDictManagerDialog(HWND hwndOwner)
    {
        DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_DICT_MGR), hwndOwner, DictMgrDlgProc, 0);
    }
}

// ===== 设置对话框 =====

static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        CTextService* pService = reinterpret_cast<CTextService*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pService);

        HWND hCombo = GetDlgItem(hDlg, IDC_INPUT_MODE_COMBO);
        if (hCombo)
        {
            ComboBox_AddString(hCombo, L"全拼");
            ComboBox_AddString(hCombo, L"双拼");
            int sel = (pService && pService->GetPinyinMode() == IME_PINYIN_DOUBLE) ? 1 : 0;
            ComboBox_SetCurSel(hCombo, sel);
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDOK)
        {
            CTextService* pService =
                reinterpret_cast<CTextService*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            HWND hCombo = GetDlgItem(hDlg, IDC_INPUT_MODE_COMBO);
            int sel = hCombo ? ComboBox_GetCurSel(hCombo) : 0;
            if (pService)
                pService->SetPinyinMode((sel == 1) ? IME_PINYIN_DOUBLE : IME_PINYIN_FULL);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_MANAGE_DICTS_BTN)
        {
            ShowDictManagerDialog(hDlg);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// 打开模态"设置"对话框。自绘状态条（StatusBar.cpp）与语言栏按钮共用此入口。
void M2CShowSettingsDialog(CTextService* pService, HWND hwndOwner)
{
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_SETTINGS),
                    hwndOwner, SettingsDlgProc, (LPARAM)pService);
}

// ===== CLangBarSettingsButton =====

CLangBarSettingsButton::CLangBarSettingsButton(CTextService* pService)
    : _cRef(1)
    , _pService(pService)
    , _pSink(nullptr)
    , _dwSinkCookie(TF_INVALID_COOKIE)
    , _hIcon(nullptr)
{
    DllAddRef();
}

CLangBarSettingsButton::~CLangBarSettingsButton()
{
    if (_hIcon) { DestroyIcon(_hIcon); _hIcon = nullptr; }
    DllRelease();
}

STDMETHODIMP CLangBarSettingsButton::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_INVALIDARG;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
    {
        *ppvObj = (ITfLangBarItemButton*)this;
    }
    else if (IsEqualIID(riid, IID_ITfSource))
    {
        *ppvObj = (ITfSource*)this;
    }
    else
    {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CLangBarSettingsButton::AddRef()  { return InterlockedIncrement(&_cRef); }
STDMETHODIMP_(ULONG) CLangBarSettingsButton::Release()
{
    LONG c = InterlockedDecrement(&_cRef);
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP CLangBarSettingsButton::GetInfo(TF_LANGBARITEMINFO* pInfo)
{
    if (!pInfo) return E_INVALIDARG;
    pInfo->clsidService = CLSID_M2CTextService;
    pInfo->guidItem = GUID_M2CLangBarSettingsButton;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 4;
    wcscpy_s(pInfo->szDescription, M2C_SETTINGS_BTN_DESC);
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::GetStatus(DWORD* pdwStatus)
{
    if (!pdwStatus) return E_INVALIDARG;
    *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::Show(BOOL /*fShow*/) { return S_OK; }

STDMETHODIMP CLangBarSettingsButton::GetTooltipString(BSTR* pbstrToolTip)
{
    if (!pbstrToolTip) return E_INVALIDARG;
    *pbstrToolTip = SysAllocString(L"设置");
    return (*pbstrToolTip) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarSettingsButton::OnClick(TfLBIClick /*click*/, POINT /*pt*/, const RECT* /*prcArea*/)
{
    if (_pService)
        M2CShowSettingsDialog(_pService, nullptr);
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::InitMenu(ITfMenu* /*pMenu*/) { return S_OK; }
STDMETHODIMP CLangBarSettingsButton::OnMenuSelect(UINT /*wID*/) { return S_OK; }

STDMETHODIMP CLangBarSettingsButton::GetIcon(HICON* phIcon)
{
    if (!phIcon) return E_INVALIDARG;
    *phIcon = _GetSettingsIcon();
    return (*phIcon) ? S_OK : E_FAIL;
}

HICON CLangBarSettingsButton::_GetSettingsIcon()
{
    if (_hIcon == nullptr)
        _hIcon = _CreateSettingsIcon(M2C_SETTINGS_GLYPH, RGB(90, 90, 90));
    return _hIcon;
}

HICON CLangBarSettingsButton::_CreateSettingsIcon(const WCHAR* text, COLORREF bg)
{
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = cx;
    bi.bV5Height      = -cy;
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return nullptr;

    void* bits = nullptr;
    HBITMAP hbmColor = CreateDIBSection(hdcScreen, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);

    BITMAPINFO maskBmi = {};
    maskBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    maskBmi.bmiHeader.biWidth       = cx;
    maskBmi.bmiHeader.biHeight      = -cy;
    maskBmi.bmiHeader.biPlanes      = 1;
    maskBmi.bmiHeader.biBitCount    = 1;
    maskBmi.bmiHeader.biCompression = BI_RGB;
    void* maskBits = nullptr;
    HBITMAP hbmMask = CreateDIBSection(hdcScreen, &maskBmi, DIB_RGB_COLORS, &maskBits, nullptr, 0);

    if (!hbmColor || !hbmMask || !bits || !maskBits)
    {
        if (hbmColor) DeleteObject(hbmColor);
        if (hbmMask)  DeleteObject(hbmMask);
        ReleaseDC(nullptr, hdcScreen);
        return nullptr;
    }

    ZeroMemory(bits, (size_t)cx * (size_t)cy * 4);
    ZeroMemory(maskBits, (size_t)(((cx + 31) / 32) * 4) * (size_t)cy);

    HDC memDC = CreateCompatibleDC(hdcScreen);
    HGDIOBJ oldBmp = SelectObject(memDC, hbmColor);

    HBRUSH brush = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, bg);
    HGDIOBJ oldBrush = SelectObject(memDC, brush);
    HGDIOBJ oldPen = SelectObject(memDC, pen);
    RoundRect(memDC, 0, 0, cx, cy, 5, 5);

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    HFONT font = CreateFontW(-MulDiv(9, GetDeviceCaps(hdcScreen, LOGPIXELSY), 72), 0, 0, 0,
                             FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(memDC, font);
    RECT rc = { 0, 0, cx, cy };
    DrawTextW(memDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    DWORD* px = (DWORD*)bits;
    for (int i = 0; i < cx * cy; ++i)
    {
        if ((px[i] & 0x00FFFFFF) != 0)
            px[i] |= 0xFF000000;
    }

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = hbmMask;
    ii.hbmColor = hbmColor;
    HICON hIcon = CreateIconIndirect(&ii);

    SelectObject(memDC, oldFont);
    SelectObject(memDC, oldBrush);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBmp);
    DeleteObject(font);
    DeleteObject(brush);
    DeleteObject(pen);
    DeleteDC(memDC);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    ReleaseDC(nullptr, hdcScreen);
    return hIcon;
}

STDMETHODIMP CLangBarSettingsButton::GetText(BSTR* pbstrText)
{
    if (!pbstrText) return E_INVALIDARG;
    *pbstrText = SysAllocString(M2C_SETTINGS_GLYPH);
    return (*pbstrText) ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarSettingsButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie)
{
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink))
        return CONNECT_E_CANNOTCONNECT;
    if (_pSink) return CONNECT_E_ADVISELIMIT;
    if (!punk || !pdwCookie) return E_INVALIDARG;

    if (FAILED(punk->QueryInterface(IID_ITfLangBarItemSink, (void**)&_pSink)))
        return E_NOINTERFACE;

    _dwSinkCookie = 0x53455447; // 'SETG'
    *pdwCookie = _dwSinkCookie;
    return S_OK;
}

STDMETHODIMP CLangBarSettingsButton::UnadviseSink(DWORD dwCookie)
{
    if (dwCookie != _dwSinkCookie || !_pSink)
        return CONNECT_E_NOCONNECTION;
    _pSink->Release();
    _pSink = nullptr;
    _dwSinkCookie = TF_INVALID_COOKIE;
    return S_OK;
}

// KeyHandler.cpp —— 按键事件处理、edit session、引擎对接
#include "Globals.h"
#include "TextService.h"
#include "CandidateWindow.h"
#include <functional>

// 通用 edit session 包装
class CEditSession : public ITfEditSession
{
public:
    CEditSession(std::function<HRESULT(TfEditCookie)> fn) : _cRef(1), _fn(fn) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
    {
        if (!ppvObj) return E_INVALIDARG;
        *ppvObj = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfEditSession))
        {
            *ppvObj = this; AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_cRef); }
    STDMETHODIMP_(ULONG) Release() override
    {
        LONG c = InterlockedDecrement(&_cRef);
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { return _fn(ec); }

private:
    LONG _cRef;
    std::function<HRESULT(TfEditCookie)> _fn;
};

// ================= 按键是否消费判断 =================

// 标点映射（vk + shift 状态）：*pCn 为中文标点、*pEn 为英文标点，返回 FALSE 表示无映射
BOOL CTextService::_MapPunct(WPARAM vk, WCHAR* pCn, WCHAR* pEn)
{
    WCHAR cn = 0, en = 0;
    bool fShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (fShift)
    {
        switch (vk)
        {
        case '1':           cn = 0xFF01;   en = L'!';  break; // ！
        case '9':           cn = 0xFF08;   en = L'(';  break; // （
        case '0':           cn = 0xFF09;   en = L')';  break; // ）
        case VK_OEM_COMMA:  cn = 0x300A;   en = L'<';  break; // 《
        case VK_OEM_PERIOD: cn = 0x300B;   en = L'>';  break; // 》
        case VK_OEM_1:      cn = 0xFF1A;   en = L':';  break; // ：
        case VK_OEM_2:      cn = 0xFF1F;   en = L'?';  break; // ？
        case VK_OEM_7:      cn = 0xFF02;   en = L'"';  break; // ＂
        }
    }
    else
    {
        switch (vk)
        {
        case VK_OEM_COMMA:  cn = 0xFF0C;   en = L',';  break; // ，
        case VK_OEM_PERIOD: cn = 0x3002;   en = L'.';  break; // 。
        case VK_OEM_1:      cn = 0xFF1B;   en = L';';  break; // ；
        case VK_OEM_2:      cn = 0xFF0F;   en = L'/';  break; // ／
        case VK_OEM_3:      cn = 0xFF40;   en = L'`';  break; // ｀
        case VK_OEM_4:      cn = 0x3010;   en = L'[';  break; // 【
        case VK_OEM_5:      cn = 0xFF3C;   en = L'\\'; break; // ＼
        case VK_OEM_6:      cn = 0x3011;   en = L']';  break; // 】
        case VK_OEM_7:      cn = 0xFF07;   en = L'\''; break; // ＇
        }
    }
    if (cn == 0) return FALSE;
    if (pCn) *pCn = cn;
    if (pEn) *pEn = en;
    return TRUE;
}

void CTextService::_ClearPunctPending()
{
    _punctPending = false;
    _punctInComp = false;
    _punctLen = 0;
    _punctCn[0] = 0;
    _punctEn[0] = 0;
}

void CTextService::_AppendPunct(WCHAR cn, WCHAR en)
{
    if (_punctLen >= (int)ARRAYSIZE(_punctCn) - 1)
        return;
    _punctCn[_punctLen] = cn;
    _punctEn[_punctLen] = en;
    ++_punctLen;
    _punctCn[_punctLen] = 0;
    _punctEn[_punctLen] = 0;
}

void CTextService::_RemoveLastPunct()
{
    if (_punctLen <= 1)
    {
        _ClearPunctPending();
        return;
    }
    --_punctLen;
    _punctCn[_punctLen] = 0;
    _punctEn[_punctLen] = 0;
}

static bool IsCtrlAltDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000);
}

BOOL CTextService::_IsKeyEaten(WPARAM wParam, bool /*fTest*/)
{
    // Shift 永远不消费（用于 OnKeyUp 切换中英文）
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT)
        return FALSE;

    if (IsCtrlAltDown()) return FALSE;   // 快捷键放行

    // 英文模式：完全不拦截，系统原生输出
    if (_mode == IME_MODE_ENGLISH)
        return FALSE;

    bool fHasComp = IME_HasComposition();

    // 标点候选态：空格/回车/数字/ESC/退格/标点/字母都消费
    if (_punctPending)
    {
        if (wParam == VK_SPACE || wParam == VK_RETURN ||
            wParam == VK_ESCAPE || wParam == VK_BACK)
            return TRUE;
        if (wParam >= '0' && wParam <= '9')
            return TRUE;
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en)) return TRUE;
        if (wParam >= 'A' && wParam <= 'Z')
            return (GetKeyState(VK_SHIFT) & 0x8000) ? FALSE : TRUE;
        return FALSE;
    }

    // 字母：Shift+字母 放行（输出大写英文）
    if (wParam >= 'A' && wParam <= 'Z')
        return (GetKeyState(VK_SHIFT) & 0x8000) ? FALSE : TRUE;

    if (wParam == VK_BACK || wParam == VK_ESCAPE || wParam == VK_RETURN)
        return fHasComp ? TRUE : FALSE;

    if (wParam == VK_SPACE)
        return TRUE; // 有组合串提交首候选；无组合串插入空格

    // 标点：任何情况下都消费，只在候选窗显示，绝不直接上屏
    // （无组合串 → 中文/英文标点候选；有组合串 → 挂到拼音末尾，选词后一起上屏）
    {
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
            return TRUE;
    }

    // 数字选词
    if (wParam >= '1' && wParam <= '9')
    {
        if (!fHasComp) return FALSE;
        int idx = _pageIndex * M2C_PAGE_SIZE + (int)(wParam - '1');
        return (idx < IME_GetCandidateCount()) ? TRUE : FALSE;
    }

    // 翻页键
    if (wParam == VK_PRIOR || wParam == VK_NEXT ||
        wParam == VK_UP || wParam == VK_DOWN ||
        wParam == VK_OEM_MINUS || wParam == VK_OEM_PLUS)
    {
        return fHasComp ? TRUE : FALSE;
    }

    return FALSE;
}

// ================= TSF 按键回调 =================

STDMETHODIMP CTextService::OnSetFocus(BOOL fForeground)
{
    if (fForeground)
    {
        // 切到本输入法：清组合串、标点候选、隐藏候选窗
        IME_ClearComposition();
        _ClearPunctPending();
        _pageIndex = 0;
        if (_pCandidateWnd) _pCandidateWnd->_Hide();
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;

    // Shift 状态跟踪（OnTestKeyDown 对未消费的键也会被调用，适合记录）
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT)
    {
        _shiftPressed = true;
        *pfEaten = FALSE;
        return S_OK;
    }
    // 任何其它键按下都取消"单独按 Shift"的判定
    _shiftPressed = false;

    *pfEaten = _IsKeyEaten(wParam, true);
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = _IsKeyEaten(wParam, false);
    if (!*pfEaten) return S_OK;

    if (pContext == nullptr) { *pfEaten = FALSE; return S_OK; }

    _keyHandledInSession = false;
    _hasCaretRect = false;

    CEditSession session([this, pContext, wParam](TfEditCookie ec) -> HRESULT
    {
        _HandleKeyInSession(ec, pContext, wParam);
        RECT rc = {};
        if (_GetCaretRect(ec, pContext, &rc))
        {
            _caretRect = rc;
            _hasCaretRect = true;
        }
        return S_OK;
    });

    HRESULT hrSession = E_FAIL;
    HRESULT hr = pContext->RequestEditSession(_tfClientId, &session, TF_ES_READWRITE | TF_ES_SYNC, &hrSession);
    if (FAILED(hr))
    {
        // 同步会话被拒（应用忙）：放弃本次按键
        *pfEaten = FALSE;
        return S_OK;
    }

    _UpdateCandidateWindow(pContext, _hasCaretRect ? &_caretRect : nullptr);
    return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* /*pContext*/, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;

    // Shift 抬起且期间未按其它键：切换 英文 <-> 最近中文模式
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT)
    {
        if (_shiftPressed)
        {
            _shiftPressed = false;
            if (_mode == IME_MODE_ENGLISH)
                SetMode(_lastChineseMode);
            else
                SetMode(IME_MODE_ENGLISH);
        }
    }
    return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext* /*pContext*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext* /*pContext*/, REFGUID /*rguid*/, BOOL* pfEaten)
{
    if (pfEaten) *pfEaten = FALSE;
    return S_OK;
}

// ================= 会话内处理 =================

void CTextService::_HandleKeyInSession(TfEditCookie ec, ITfContext* pContext, WPARAM wParam)
{
    bool fHasComp = IME_HasComposition();

    // 组合串已消失：挂在末尾的标点一并作废
    if (_punctInComp && !fHasComp)
        _ClearPunctPending();

    // 0) 标点挂在拼音组合串末尾：只在候选窗显示，选词后与词一起上屏
    if (_punctPending && _punctInComp && fHasComp)
    {
        // ESC：整体取消
        if (wParam == VK_ESCAPE)
        {
            IME_ClearComposition();
            _ClearPunctPending();
            _pageIndex = 0;
            _keyHandledInSession = true;
            return;
        }

        // 退格：只去掉末尾的标点，拼音保留
        if (wParam == VK_BACK)
        {
            _RemoveLastPunct();
            _keyHandledInSession = true;
            return;
        }

        // 再输入一个标点：追加到末尾（不自动上屏）
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
        {
            _AppendPunct(cn, en);
            _keyHandledInSession = true;
            return;
        }

        // 空格 / 回车 / 数字：选词，词与中文标点一起上屏
        if (wParam == VK_SPACE || wParam == VK_RETURN ||
            (wParam >= '1' && wParam <= '9'))
        {
            WCHAR committed[64] = { 0 };
            bool fCommitted = false;
            if (wParam == VK_SPACE || wParam == VK_RETURN)
            {
                if (IME_OnChar(wParam == VK_SPACE ? ' ' : '\r'))
                {
                    IME_GetCommitted(committed, ARRAYSIZE(committed));
                    IME_ClearCommitted();
                    fCommitted = true;
                }
            }
            else
            {
                int idx = _pageIndex * M2C_PAGE_SIZE + (int)(wParam - '1');
                if (IME_SelectCandidate(idx))
                {
                    IME_GetCommitted(committed, ARRAYSIZE(committed));
                    IME_ClearCommitted();
                    fCommitted = true;
                }
            }
            if (fCommitted)
            {
                if (committed[0])
                    _InsertTextAtSelection(ec, pContext, committed, (LONG)wcslen(committed));
                _pageIndex = 0;
                if (!IME_HasComposition())
                {
                    // 拼音已全部选完：标点随词一起上屏
                    _InsertTextAtSelection(ec, pContext, _punctCn, _punctLen);
                    _ClearPunctPending();
                }
                // 否则还有剩余拼音（如 nihaoo → 选「你好」后剩 o），
                // 标点继续挂在末尾，等剩余部分选完再上屏
            }
            _keyHandledInSession = true;
            return;
        }

        // 字母等其它键：先提交当前词，再用该键开始新的拼音
        WCHAR committed[64] = { 0 };
        if (IME_OnChar(' '))
        {
            IME_GetCommitted(committed, ARRAYSIZE(committed));
            IME_ClearCommitted();
        }
        if (committed[0])
            _InsertTextAtSelection(ec, pContext, committed, (LONG)wcslen(committed));
        _pageIndex = 0;
        if (!IME_HasComposition())
        {
            _InsertTextAtSelection(ec, pContext, _punctCn, _punctLen);
            _ClearPunctPending();
        }
        fHasComp = IME_HasComposition();
    }

    // 0.1) 标点候选态（无拼音）：等待用户选择中文 / 英文标点
    if (_punctPending && !_punctInComp)
    {
        if (wParam == VK_ESCAPE)
        {
            _ClearPunctPending();
            _pageIndex = 0;
            _keyHandledInSession = true;
            return;
        }

        // 退格：去掉最后一个标点
        if (wParam == VK_BACK)
        {
            _RemoveLastPunct();
            _keyHandledInSession = true;
            return;
        }

        // 又输入一个标点：追加到候选串末尾（不自动上屏）
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
        {
            _AppendPunct(cn, en);
            _keyHandledInSession = true;
            return;
        }

        // 空格 / 回车 / 数字：确认标点候选
        if (wParam == VK_SPACE || wParam == VK_RETURN ||
            (wParam >= '0' && wParam <= '9'))
        {
            if (wParam == VK_SPACE || wParam == VK_RETURN || wParam == '1')
                _InsertTextAtSelection(ec, pContext, _punctCn, _punctLen);   // 默认第一个候选：中文标点
            else if (wParam == '2')
                _InsertTextAtSelection(ec, pContext, _punctEn, _punctLen);
            else
            {
                WCHAR d = (WCHAR)wParam;                                     // 无对应候选：直接输入数字
                _InsertTextAtSelection(ec, pContext, &d, 1);
            }
            _ClearPunctPending();
            _keyHandledInSession = true;
            return;
        }

        // 其它键（字母等）：退出标点候选，继续走拼音流程
        _ClearPunctPending();
    }

    // 1) 翻页
    if (wParam == VK_PRIOR || wParam == VK_UP || wParam == VK_OEM_MINUS)
    {
        if (_pageIndex > 0) --_pageIndex;
        _keyHandledInSession = true;
        return;
    }
    if (wParam == VK_NEXT || wParam == VK_DOWN || wParam == VK_OEM_PLUS)
    {
        int count = IME_GetCandidateCount();
        int maxPage = (count > 0) ? (count - 1) / M2C_PAGE_SIZE : 0;
        if (_pageIndex < maxPage) ++_pageIndex;
        _keyHandledInSession = true;
        return;
    }

    // 2) ESC：清空
    if (wParam == VK_ESCAPE)
    {
        IME_ClearComposition();
        _pageIndex = 0;
        _keyHandledInSession = true;
        return;
    }

    // 3) 退格
    if (wParam == VK_BACK)
    {
        IME_OnBackspace();
        // 删空后回到第一页
        int count = IME_GetCandidateCount();
        int maxPage = (count > 0) ? (count - 1) / M2C_PAGE_SIZE : 0;
        if (_pageIndex > maxPage) _pageIndex = maxPage;
        _keyHandledInSession = true;
        return;
    }

    // 4) 数字选词
    if (wParam >= '1' && wParam <= '9' && fHasComp)
    {
        int idx = _pageIndex * M2C_PAGE_SIZE + (int)(wParam - '1');
        if (IME_SelectCandidate(idx))
        {
            _pageIndex = 0;
            WCHAR buf[64] = { 0 };
            IME_GetCommitted(buf, ARRAYSIZE(buf));
            IME_ClearCommitted();
            if (buf[0])
                _InsertTextAtSelection(ec, pContext, buf, (LONG)wcslen(buf));
        }
        _keyHandledInSession = true;
        return;
    }

    // 5) 标点：进入候选（有拼音时挂到拼音末尾，选词后一起上屏）
    {
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
        {
            // 从空序列开始，追加第一个标点
            _ClearPunctPending();
            _punctPending = true;
            _punctInComp = fHasComp;
            _AppendPunct(cn, en);
            _keyHandledInSession = true;
            return;
        }
    }

    // 6) 字母 / 空格 / 回车：交给引擎
    char ch = 0;
    if (wParam >= 'A' && wParam <= 'Z')
        ch = (char)(wParam - 'A' + 'a');
    else if (wParam == VK_SPACE)
        ch = ' ';
    else if (wParam == VK_RETURN)
        ch = '\r';

    if (ch != 0)
    {
        if (IME_OnChar(ch))
        {
            WCHAR buf[64] = { 0 };
            IME_GetCommitted(buf, ARRAYSIZE(buf));
            IME_ClearCommitted();
            if (buf[0])
            {
                _InsertTextAtSelection(ec, pContext, buf, (LONG)wcslen(buf));
                _pageIndex = 0;
            }
        }
        _keyHandledInSession = true;
    }
}

// ================= 文本插入 =================

HRESULT CTextService::_InsertTextAtSelection(TfEditCookie ec, ITfContext* pContext, const WCHAR* text, LONG len)
{
    if (!text || len <= 0) return S_FALSE;

    ITfInsertAtSelection* pIAS = nullptr;
    HRESULT hr = pContext->QueryInterface(IID_ITfInsertAtSelection, (void**)&pIAS);
    if (FAILED(hr) || !pIAS) return hr;

    ITfRange* pRange = nullptr;
    hr = pIAS->InsertTextAtSelection(ec, 0, text, len, &pRange);
    if (SUCCEEDED(hr) && pRange)
    {
        // 把插入点移到文本末尾
        pRange->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel;
        sel.range = pRange;
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        pContext->SetSelection(ec, 1, &sel);
        pRange->Release();
    }
    pIAS->Release();
    return hr;
}

// ================= 光标定位 =================

BOOL CTextService::_GetCaretRect(TfEditCookie ec, ITfContext* pContext, RECT* prc)
{
    if (!prc) return FALSE;

    TF_SELECTION sel = {};
    ULONG fetched = 0;
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched == 0)
        return FALSE;

    BOOL ok = FALSE;
    ITfContextView* pView = nullptr;
    if (SUCCEEDED(pContext->GetActiveView(&pView)) && pView)
    {
        BOOL fClipped = FALSE;
        RECT rc = {};
        if (SUCCEEDED(pView->GetTextExt(ec, sel.range, &rc, &fClipped)))
        {
            HWND hwnd = nullptr;
            if (SUCCEEDED(pView->GetWnd(&hwnd)) && hwnd)
            {
                POINT pt = { rc.left, rc.bottom };
                ClientToScreen(hwnd, &pt);
                prc->left = rc.left + (pt.x - rc.left);
                prc->right = prc->left + (rc.right - rc.left);
                prc->bottom = pt.y;
                prc->top = prc->bottom - (rc.bottom - rc.top);
                ok = TRUE;
            }
        }
        pView->Release();
    }
    if (sel.range) sel.range->Release();
    return ok;
}

// ================= 候选窗刷新 =================

void CTextService::_UpdateCandidateWindow(ITfContext* /*pContext*/, const RECT* pCaretRect)
{
    if (!_pCandidateWnd) return;

    if (!IME_HasComposition())
    {
        if (_punctInComp) _ClearPunctPending();  // 组合串已消失，挂着的标点作废
        if (!_punctPending)
        {
            _pCandidateWnd->_Hide();
            return;
        }
    }

    std::wstring comp;
    std::vector<std::pair<std::wstring, int>> items;
    int totalPages = 1;

    if (_punctPending && !_punctInComp)
    {
        // 标点候选（无拼音）：第 1 项中文标点（默认），第 2 项英文标点
        comp = _punctEn;
        items.emplace_back(_punctCn, 0);
        items.emplace_back(_punctEn, 0);
    }
    else
    {
        // 拼音串（若末尾挂着标点，一并显示在组合串上）
        WCHAR bufComp[128] = { 0 };
        IME_GetComposition(bufComp, ARRAYSIZE(bufComp));
        comp = bufComp;
        if (_punctPending && _punctInComp)
            comp += _punctEn;

        // 当前页候选
        int count = IME_GetCandidateCount();
        int maxPage = (count > 0) ? (count - 1) / M2C_PAGE_SIZE : 0;
        if (_pageIndex > maxPage) _pageIndex = maxPage;
        totalPages = maxPage + 1;

        int start = _pageIndex * M2C_PAGE_SIZE;
        int end = min(start + M2C_PAGE_SIZE, count);
        for (int i = start; i < end; ++i)
        {
            WCHAR buf[64] = { 0 };
            IME_GetCandidate(i, buf, ARRAYSIZE(buf));
            items.emplace_back(buf, IME_GetCandidateSource(i));
        }
    }

    // 光标位置：session 里取到的优先，否则用 GUIThreadInfo 兜底
    RECT caret = { 0 };
    bool hasCaret = false;
    if (pCaretRect)
    {
        caret = *pCaretRect;
        hasCaret = true;
    }
    else
    {
        GUITHREADINFO gti = {};
        gti.cbSize = sizeof(gti);
        if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndCaret)
        {
            caret = gti.rcCaret;
            ClientToScreen(gti.hwndCaret, (POINT*)&caret);
            ClientToScreen(gti.hwndCaret, (POINT*)&caret + 1);
            hasCaret = true;
        }
    }

    _pCandidateWnd->_Update(comp, items, _punctPending ? 0 : _pageIndex, totalPages,
                            hasCaret ? &caret : nullptr);
}

// ================= 鼠标点击选词（候选窗回调） =================

void CTextService::OnCandidateClicked(int itemIndexOnPage)
{
    if (_mode == IME_MODE_ENGLISH) return;

    // 标点候选态：无拼音时 0=中文标点（默认）、1=英文标点；
    // 有拼音时先选词，再把挂在末尾的中文标点一起上屏
    std::wstring punctOut;
    bool fAppendAfterSelect = false;
    int idx = -1;
    if (_punctPending && _punctInComp)
    {
        punctOut = _punctCn;
        fAppendAfterSelect = true;
        idx = _pageIndex * M2C_PAGE_SIZE + itemIndexOnPage;
        if (idx >= IME_GetCandidateCount()) return;
    }
    else if (_punctPending)
    {
        if (itemIndexOnPage == 0)      punctOut = _punctCn;
        else if (itemIndexOnPage == 1) punctOut = _punctEn;
        else return;
        if (punctOut.empty()) return;
    }
    else
    {
        idx = _pageIndex * M2C_PAGE_SIZE + itemIndexOnPage;
        if (idx >= IME_GetCandidateCount()) return;
    }

    ITfDocumentMgr* pDocMgr = nullptr;
    if (FAILED(_pThreadMgr->GetFocus(&pDocMgr)) || !pDocMgr) return;

    ITfContext* pContext = nullptr;
    RECT caretRect = {};
    bool hasCaretRect = false;
    if (SUCCEEDED(pDocMgr->GetBase(&pContext)) && pContext)
    {
        CEditSession session([this, pContext, idx, punctOut, fAppendAfterSelect, &caretRect, &hasCaretRect](TfEditCookie ec) -> HRESULT
        {
            if (fAppendAfterSelect)
            {
                if (IME_SelectCandidate(idx))
                {
                    WCHAR buf[64] = { 0 };
                    IME_GetCommitted(buf, ARRAYSIZE(buf));
                    IME_ClearCommitted();
                    if (buf[0])
                        _InsertTextAtSelection(ec, pContext, buf, (LONG)wcslen(buf));
                    // 只有全部拼音选完，标点才随词上屏；否则继续挂在剩余拼音末尾
                    if (!punctOut.empty() && !IME_HasComposition())
                        _InsertTextAtSelection(ec, pContext, punctOut.c_str(), (LONG)punctOut.size());
                }
            }
            else if (!punctOut.empty())
            {
                _InsertTextAtSelection(ec, pContext, punctOut.c_str(), (LONG)punctOut.size());
            }
            else if (IME_SelectCandidate(idx))
            {
                WCHAR buf[64] = { 0 };
                IME_GetCommitted(buf, ARRAYSIZE(buf));
                IME_ClearCommitted();
                if (buf[0])
                    _InsertTextAtSelection(ec, pContext, buf, (LONG)wcslen(buf));
            }
            RECT rc = {};
            if (_GetCaretRect(ec, pContext, &rc))
            {
                caretRect = rc;
                hasCaretRect = true;
            }
            return S_OK;
        });
        HRESULT hrSession = E_FAIL;
        pContext->RequestEditSession(_tfClientId, &session, TF_ES_READWRITE | TF_ES_SYNC, &hrSession);
        pContext->Release();
    }
    pDocMgr->Release();

    _pageIndex = 0;
    if (fAppendAfterSelect)
    {
        // 还有剩余拼音时保留标点挂载状态
        if (!IME_HasComposition()) _ClearPunctPending();
    }
    else if (!punctOut.empty())
    {
        _ClearPunctPending();
    }
    if (_pCandidateWnd)
    {
        // 选词后若组合串仍有剩余拼音，保持候选窗并刷新候选（连续选词）
        if (IME_HasComposition())
            _UpdateCandidateWindow(nullptr, hasCaretRect ? &caretRect : nullptr);
        else
            _pCandidateWnd->_Hide();
    }
}

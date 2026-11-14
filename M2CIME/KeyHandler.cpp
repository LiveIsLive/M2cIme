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

// 返回指定标点字符的全部候选形式（候选顺序根据当前中文/英文标点状态动态排列）
// 当前每种标点默认返回 2 种：中文 / 英文；引号类（' "）额外返回中文左右配对
static int _BuildPunctCandidates(WCHAR cn, WCHAR en, bool fFullwidth,
                                 std::vector<std::wstring>& out)
{
    out.clear();
    // 先按状态排好 cn / en
    if (fFullwidth)
    {
        out.push_back(std::wstring(1, cn));
        out.push_back(std::wstring(1, en));
    }
    else
    {
        out.push_back(std::wstring(1, en));
        out.push_back(std::wstring(1, cn));
    }

    // 引号类：补充中文左右配对的双引号 / 单引号
    if (en == L'"')
    {
        out.push_back(L"\x201C");   // " 左
        out.push_back(L"\x201D");   // " 右
    }
    else if (en == L'\'')
    {
        out.push_back(L"\x2018");   // ' 左
        out.push_back(L"\x2019");   // ' 右
    }
    return (int)out.size();
}

// 英文标点字符 → 对应中文标点（与 _MapPunct 的映射保持一致）
static bool _MapPunctChar(WCHAR en, WCHAR* pCn)
{
    WCHAR cn = 0;
    switch (en)
    {
    case L'!':  cn = 0xFF01; break;
    case L'(':  cn = 0xFF08; break;
    case L')':  cn = 0xFF09; break;
    case L'<':  cn = 0x300A; break;
    case L'>':  cn = 0x300B; break;
    case L':':  cn = 0xFF1A; break;
    case L'?':  cn = 0xFF1F; break;
    case L'"':  cn = 0xFF02; break;
    case L',':  cn = 0xFF0C; break;
    case L'.':  cn = 0x3002; break;
    case L';':  cn = 0xFF1B; break;
    case L'/':  cn = 0xFF0F; break;
    case L'`':  cn = 0xFF40; break;
    case L'[':  cn = 0x3010; break;
    case L'\\': cn = 0xFF3C; break;
    case L']':  cn = 0x3011; break;
    case L'\'': cn = 0xFF07; break;
    default:    return false;
    }
    if (pCn) *pCn = cn;
    return true;
}

// 需要直接上屏的首字符标点（半角形式）：, . ! ?
// 这 4 个标点（含全角 ，。！？）不允许成为组合串/候选框上的第 1 个字符
static bool _IsLeadingDirectPunct(WCHAR en)
{
    return en == L',' || en == L'.' || en == L'!' || en == L'?';
}

// 组合串是否以标点开头（从头选词时，先处理最前面的标点）
static bool _StartsWithPunct(const std::wstring& s, WCHAR* pPunct)
{
    if (pPunct) *pPunct = 0;
    if (s.empty()) return false;
    const WCHAR c = s[0];
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))
        return false;
    if (pPunct) *pPunct = c;
    return true;
}

// 选词后组合串被消费掉的前缀里，取出标点字符（保持输入时的原样）
static std::wstring _TakeConsumedPunct(const std::wstring& before, const std::wstring& after)
{
    size_t consumed = (before.size() > after.size()) ? before.size() - after.size() : 0;
    std::wstring out;
    for (size_t i = 0; i < consumed && i < before.size(); ++i)
    {
        const WCHAR c = before[i];
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) continue;
        out.push_back(c);
    }
    return out;
}

// 在混合态候选窗中，决定展示"哪个"标点的候选：优先前标点，没有则后标点
// 返回值：cn / en 字符；*pSide 指明来自前还是后标点
void CTextService::_GetMixedPunctSource(WCHAR* pCn, WCHAR* pEn, PunctSide* pSide) const
{
    *pSide = PSIDE_NONE;
    *pCn = 0; *pEn = 0;
    if (_punctBeforeLen > 0)
    {
        *pCn = _punctBeforeCn[0];
        *pEn = _punctBeforeEn[0];
        *pSide = PSIDE_BEFORE;
        return;
    }
    if (_punctAfterLen > 0)
    {
        *pCn = _punctAfterCn[0];
        *pEn = _punctAfterEn[0];
        *pSide = PSIDE_AFTER;
    }
}

void CTextService::_ClearPunctPending()
{
    _punctPending = false;
    _punctInComp = false;
    _punctBeforeLen = 0;
    _punctAfterLen = 0;
    _punctAfterPos = 0;
    _mixedPunctCandCount = 0;
    _punctBeforeCn[0] = 0;
    _punctBeforeEn[0] = 0;
    _punctAfterCn[0] = 0;
    _punctAfterEn[0] = 0;
}

// 前标点追加（纯标点候选态 or 先输标点后输拼音时的 leading 标点）
void CTextService::_AppendPunctBefore(WCHAR cn, WCHAR en)
{
    if (_punctBeforeLen >= (int)ARRAYSIZE(_punctBeforeCn) - 1)
        return;
    _punctBeforeCn[_punctBeforeLen] = cn;
    _punctBeforeEn[_punctBeforeLen] = en;
    ++_punctBeforeLen;
    _punctBeforeCn[_punctBeforeLen] = 0;
    _punctBeforeEn[_punctBeforeLen] = 0;
}

// 后标点追加（组合串存在时再输的标点 → trailing 标点）
void CTextService::_AppendPunctAfter(WCHAR cn, WCHAR en)
{
    if (_punctAfterLen >= (int)ARRAYSIZE(_punctAfterCn) - 1)
        return;
    _punctAfterCn[_punctAfterLen] = cn;
    _punctAfterEn[_punctAfterLen] = en;
    ++_punctAfterLen;
    _punctAfterCn[_punctAfterLen] = 0;
    _punctAfterEn[_punctAfterLen] = 0;
}

void CTextService::_RemoveLastPunct()
{
    // 优先删后标点
    if (_punctAfterLen > 0)
    {
        --_punctAfterLen;
        _punctAfterCn[_punctAfterLen] = 0;
        _punctAfterEn[_punctAfterLen] = 0;
        if (_punctAfterLen == 0 && _punctBeforeLen == 0)
            _ClearPunctPending();
        return;
    }
    if (_punctBeforeLen > 0)
    {
        if (_punctBeforeLen <= 1)
        {
            _ClearPunctPending();
            return;
        }
        --_punctBeforeLen;
        _punctBeforeCn[_punctBeforeLen] = 0;
        _punctBeforeEn[_punctBeforeLen] = 0;
        if (_punctBeforeLen == 0)
        {
            _punctPending = false;
            _punctInComp = false;
        }
    }
}

static bool IsCtrlAltDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000);
}

// 判断当前是否为"纯 Ctrl+空格"（Ctrl 按下、Alt/Shift 均未按下），用于中英文模式切换
static bool IsCtrlSpaceCombo()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0
        && (GetKeyState(VK_MENU) & 0x8000) == 0
        && (GetKeyState(VK_SHIFT) & 0x8000) == 0;
}

// 判断当前是否为"纯 Ctrl+."（Ctrl 按下、Alt/Shift/Win 均未按下），用于中文/英文标点切换
static bool IsCtrlPeriodCombo()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0
        && (GetKeyState(VK_MENU) & 0x8000) == 0
        && (GetKeyState(VK_SHIFT) & 0x8000) == 0
        && (GetKeyState(VK_LWIN) & 0x8000) == 0
        && (GetKeyState(VK_RWIN) & 0x8000) == 0;
}

// 判断当前是否为"纯 Shift+空格"（Shift 按下、Ctrl/Alt/Win 均未按下），用于全角/半角切换
static bool IsShiftSpaceCombo()
{
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0
        && (GetKeyState(VK_CONTROL) & 0x8000) == 0
        && (GetKeyState(VK_MENU) & 0x8000) == 0
        && (GetKeyState(VK_LWIN) & 0x8000) == 0
        && (GetKeyState(VK_RWIN) & 0x8000) == 0;
}

// 把按键(vk + Shift 状态)映射成对应的 ASCII 可打印字符；非可打印返回 0
static char _VkToAsciiChar(WPARAM vk)
{
    bool fShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (vk >= 'A' && vk <= 'Z')
        return (char)(fShift ? vk : (vk - 'A' + 'a'));
    if (vk >= '0' && vk <= '9')
        return (char)vk;
    if (vk == VK_SPACE)
        return ' ';
    if (!fShift)
    {
        switch (vk)
        {
        case VK_OEM_MINUS:  return '-';
        case VK_OEM_PLUS:   return '=';
        case VK_OEM_COMMA:  return ',';
        case VK_OEM_PERIOD: return '.';
        case VK_OEM_1:      return ';';
        case VK_OEM_2:      return '/';
        case VK_OEM_3:      return '`';
        case VK_OEM_4:      return '[';
        case VK_OEM_5:      return '\\';
        case VK_OEM_6:      return ']';
        case VK_OEM_7:      return '\'';
        }
    }
    else
    {
        switch (vk)
        {
        case '0':           return ')';
        case '1':           return '!';
        case '2':           return '@';
        case '3':           return '#';
        case '4':           return '$';
        case '5':           return '%';
        case '6':           return '^';
        case '7':           return '&';
        case '8':           return '*';
        case '9':           return '(';
        case VK_OEM_MINUS:  return '_';
        case VK_OEM_PLUS:   return '+';
        case VK_OEM_COMMA:  return '<';
        case VK_OEM_PERIOD: return '>';
        case VK_OEM_1:      return ':';
        case VK_OEM_2:      return '?';
        case VK_OEM_3:      return '~';
        case VK_OEM_4:      return '{';
        case VK_OEM_5:      return '|';
        case VK_OEM_6:      return '}';
        case VK_OEM_7:      return '"';
        }
    }
    return 0;
}

BOOL CTextService::_IsKeyEaten(WPARAM wParam, bool /*fTest*/)
{
    // Shift 永远不消费（用于 OnKeyUp 切换中英文）
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT)
        return FALSE;

    // Ctrl+空格：中英文模式切换快捷键，消费掉（避免空格落到应用）。
    // 常规情况下该组合键被系统热键在 imm32 层拦截、走舱室路径（OnChange），
    // 此拦截仅服务于热键被禁用时的保留键/OnKeyDown 备用路径
    if (wParam == VK_SPACE && IsCtrlSpaceCombo())
        return TRUE;

    // Ctrl+.：中文/英文标点切换快捷键，消费掉（避免 '.' 落到应用）
    if (wParam == VK_OEM_PERIOD && IsCtrlPeriodCombo())
        return TRUE;

    // Shift+空格：全角/半角切换快捷键，消费掉（避免空格落到应用）
    if (wParam == VK_SPACE && IsShiftSpaceCombo())
        return TRUE;

    if (IsCtrlAltDown()) return FALSE;   // 快捷键放行

    // 英文模式：拦截标点键以便根据中文/英文标点状态转换后输出
    // 字母、数字、空格键、功能键仍然放行；全角模式下字母/数字/空格也拦截（转全角输出）
    if (_mode == IME_MODE_ENGLISH)
    {
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
            return TRUE;  // 消费标点
        if (_widthMode == IME_WIDTH_FULLWIDTH)
        {
            if ((wParam >= 'A' && wParam <= 'Z') ||
                (wParam >= '0' && wParam <= '9') ||
                wParam == VK_SPACE)
                return TRUE;
        }
        return FALSE;
    }

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

    // 标点：中文模式下统一由引擎处理（标点会进组合串，分词器跳过标点）
    // 注意：_IsKeyEaten 会被 OnTestKeyDown 和 OnKeyDown 各调用一次，
    // 这里绝不能产生副作用（不能真的往组合串里加字符），否则会被处理两遍
    {
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
            return TRUE;
    }

    // 数字选词
    if (wParam >= '1' && wParam <= '9')
    {
        if (!fHasComp) return FALSE;

        // 组合串以标点开头（从头选词）：数字选的是标点的各种写法，
        // 这些候选由前端生成，引擎的候选列表里没有，需要单独判断。
        WCHAR compBuf[128] = { 0 };
        IME_GetComposition(compBuf, ARRAYSIZE(compBuf));
        std::wstring comp(compBuf);
        WCHAR firstPunct = 0;
        if (_StartsWithPunct(comp, &firstPunct))
        {
            WCHAR cn = 0;
            std::vector<std::wstring> punctCands;
            int nCand = 0;
            if (_MapPunctChar(firstPunct, &cn))
                nCand = _BuildPunctCandidates(cn, firstPunct,
                                              _punctMode == IME_PUNCT_FULLWIDTH,
                                              punctCands);
            return ((int)(wParam - '1') < nCand) ? TRUE : FALSE;
        }

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

    // 全角模式：对本应直通应用的可打印 ASCII 转全角输出
    // （大写字母、无组合串的数字、未映射的符号）
    if (_widthMode == IME_WIDTH_FULLWIDTH && _VkToAsciiChar(wParam) != 0)
    {
        // 数字 1-9 在有组合串时用于选词，不走全角直通
        if (wParam >= '1' && wParam <= '9' && fHasComp)
            return FALSE;
        return TRUE;
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

STDMETHODIMP CTextService::OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten)
{
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = _IsKeyEaten(wParam, false);

    if (!*pfEaten) return S_OK;

    // Shift+空格：切换全角/半角，不上屏任何字符。
    // 忽略长按产生的自动重复（lParam 第 30 位），避免一次按键反复切换。
    if (wParam == VK_SPACE && IsShiftSpaceCombo())
    {
        _shiftPressed = false;   // 防止随后松开 Shift 时再触发中/英文切换
        if ((lParam & (1L << 30)) == 0)
            ToggleWidthMode();
        return S_OK;
    }

    // Ctrl+空格：切换 英文 <-> 最近中文模式（普通话/粤语），不上屏任何字符
    // （与单击 Shift 切换行为一致：英文模式切回 _lastChineseMode，中文模式切到英文）。
    // 仅当该组合键未被系统"输入法/非输入法切换"热键拦截时才会走到这里（兜底路径）。
    if (wParam == VK_SPACE && IsCtrlSpaceCombo())
    {
        if (_mode == IME_MODE_ENGLISH)
            SetMode(_lastChineseMode);
        else
            SetMode(IME_MODE_ENGLISH);
        return S_OK;
    }

    // Ctrl+.：切换中文/英文标点，不上屏任何字符。
    // 忽略长按产生的自动重复（lParam 第 30 位），避免一次按键反复切换。
    if (wParam == VK_OEM_PERIOD && IsCtrlPeriodCombo())
    {
        if ((lParam & (1L << 30)) == 0)
            TogglePunctMode();
        return S_OK;
    }

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

STDMETHODIMP CTextService::OnPreservedKey(ITfContext* /*pContext*/, REFGUID rguid, BOOL* pfEaten)
{
    if (pfEaten) *pfEaten = FALSE;

    // Ctrl+.（保留键）：切换中文/英文标点，不上屏任何字符
    if (IsEqualGUID(rguid, GUID_M2CPreservedKeyCtrlPeriod))
    {
        TogglePunctMode();
        if (pfEaten) *pfEaten = TRUE;
        return S_OK;
    }

    if (!IsEqualGUID(rguid, GUID_M2CPreservedKeyCtrlSpace))
        return S_OK;

    // Ctrl+空格（保留键）：切换 英文 <-> 最近中文模式（普通话/粤语），不上屏任何字符
    // （与单击 Shift 切换行为一致：英文模式切回 _lastChineseMode，中文模式切到英文）。
    // 仅当系统"输入法/非输入法切换"热键被用户改掉/禁用、Ctrl+空格 不再被 imm32 层
    // 拦截时才会由此路径处理（平时走 OnChange 的舱室路径）。
    if (_mode == IME_MODE_ENGLISH)
        SetMode(_lastChineseMode);
    else
        SetMode(IME_MODE_ENGLISH);

    if (pfEaten) *pfEaten = TRUE;
    return S_OK;
}

// ================= 会话内处理 =================

void CTextService::_HandleKeyInSession(TfEditCookie ec, ITfContext* pContext, WPARAM wParam)
{
    // 首字符标点规则：, . ! ?（含全角 ，。！？）不能作为组合串/候选框上的第 1 个字符。
    // 组合串为空（无拼音、无挂起标点）时直接按当前标点/全半角模式上屏，不进候选流程。
    if (_mode != IME_MODE_ENGLISH && !_punctPending && !IME_HasComposition())
    {
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en) && _IsLeadingDirectPunct(en))
        {
            WCHAR outCh = (_punctMode == IME_PUNCT_FULLWIDTH) ? cn : en;
            // 英文标点(ASCII)在全角宽度模式下转成对应全角字符
            if (_punctMode == IME_PUNCT_HALFWIDTH && _widthMode == IME_WIDTH_FULLWIDTH)
                outCh = IME_ConvertCharWidth((char)outCh);
            _InsertTextAtSelection(ec, pContext, &outCh, 1);
            _keyHandledInSession = true;
            return;
        }
    }

    // 全角模式：对"直通"的可打印 ASCII 直接转全角插入（不进拼音流程）
    // 英文模式：字母/数字/空格（标点在下方英文分支走引擎）
    // 中文模式：Shift+大写字母、无组合串的数字/空格、未映射符号
    // （无 Shift 的字母必须继续走拼音流程，否则不会弹出候选窗）
    if (_widthMode == IME_WIDTH_FULLWIDTH)
    {
        bool fDirect = false;
        char ch = 0;
        if (_mode == IME_MODE_ENGLISH)
        {
            if (wParam >= 'A' && wParam <= 'Z')      { ch = (char)(wParam - 'A' + 'a'); fDirect = true; }
            else if (wParam >= '0' && wParam <= '9') { ch = (char)wParam; fDirect = true; }
            else if (wParam == VK_SPACE)             { ch = ' '; fDirect = true; }
        }
        else
        {
            bool fShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool fHasComp = IME_HasComposition();
            if (wParam >= 'A' && wParam <= 'Z')
            {
                // 大写字母直通；无 Shift 的字母继续走下方拼音流程，避免绕过候选窗
                if (fShift)
                {
                    ch = (char)wParam;
                    fDirect = true;
                }
            }
            else if (wParam >= '0' && wParam <= '9')
            {
                if (!fHasComp)            // 无组合串的数字直通
                { ch = (char)wParam; fDirect = true; }
            }
            else if (wParam == VK_SPACE)
            {
                // 无组合串时空格输出全角空格；有组合串时交给下方逻辑提交候选词
                if (!fHasComp)
                { ch = ' '; fDirect = true; }
            }
            else
            {
                // 未被 _MapPunct 映射的可打印符号（组合串存在时 -/= 仍作翻页键）
                bool fPageKey = (wParam == VK_OEM_MINUS || wParam == VK_OEM_PLUS);
                if (!(fHasComp && fPageKey))
                {
                    WCHAR cn = 0, en = 0;
                    if (!_MapPunct(wParam, &cn, &en))
                    {
                        char c2 = _VkToAsciiChar(wParam);
                        if (c2 != 0) { ch = c2; fDirect = true; }
                    }
                }
            }
        }
        if (fDirect && ch != 0)
        {
            wchar_t wc = IME_ConvertCharWidth(ch);
            _InsertTextAtSelection(ec, pContext, &wc, 1);
            _keyHandledInSession = true;
            return;
        }
    }

    // 英文模式：直接根据中文/英文标点状态输出标点
    if (_mode == IME_MODE_ENGLISH)
    {
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
        {
            // 中文标点状态输出中文标点(cn)，英文标点状态输出英文标点(en)
            WCHAR outCh = (_punctMode == IME_PUNCT_FULLWIDTH) ? cn : en;
            // 英文标点(ASCII)在全角宽度模式下转成对应全角字符
            if (_punctMode == IME_PUNCT_HALFWIDTH && _widthMode == IME_WIDTH_FULLWIDTH)
                outCh = IME_ConvertCharWidth((char)outCh);
            if (outCh != 0)
            {
                _InsertTextAtSelection(ec, pContext, &outCh, 1);
            }
            _keyHandledInSession = true;
            return;
        }
    }

    bool fHasComp = IME_HasComposition();

    // 组合串已消失：挂在末尾的标点一并作废
    if (_punctInComp && !fHasComp)
        _ClearPunctPending();

    // 0.0) 组合串以标点开头：候选就是该标点的各种写法（从头开始选词）
    // 选中后把该写法上屏，并把组合串首位标点移除；若按字母则继续往下拼
    if (fHasComp)
    {
        WCHAR bufNow[128] = { 0 };
        IME_GetComposition(bufNow, ARRAYSIZE(bufNow));
        std::wstring compNow(bufNow);
        WCHAR firstPunct = 0;
        if (_StartsWithPunct(compNow, &firstPunct))
        {
            if (wParam == VK_ESCAPE)
            {
                IME_ClearComposition();
                _pageIndex = 0;
                _keyHandledInSession = true;
                return;
            }
            if (wParam == VK_BACK)
            {
                IME_OnBackspace();
                _pageIndex = 0;
                _keyHandledInSession = true;
                return;
            }
            if (wParam == VK_SPACE || wParam == VK_RETURN ||
                (wParam >= '1' && wParam <= '9'))
            {
                int idx = (wParam == VK_SPACE || wParam == VK_RETURN) ? 0 : (int)(wParam - '1');
                WCHAR pCn = 0;
                std::vector<std::wstring> punctCands;
                int nCand = 0;
                if (_MapPunctChar(firstPunct, &pCn))
                    nCand = _BuildPunctCandidates(pCn, firstPunct,
                                                  _punctMode == IME_PUNCT_FULLWIDTH,
                                                  punctCands);
                if (idx >= 0 && idx < nCand)
                {
                    // 只上屏选中的标点写法，然后把组合串首位的标点移除，后面继续选拼音
                    _InsertTextAtSelection(ec, pContext, punctCands[idx].c_str(), 1);
                    IME_DeleteCompositionFront(1);
                    _pageIndex = 0;
                }
                _keyHandledInSession = true;
                return;
            }
            // 其它字母键：继续往下走正常拼音流程（标点留在组合串里）
        }
    }

    // 0) 标点挂在拼音组合串上：只在候选窗显示，选词后与词一起上屏
    // 上屏顺序：_punctBeforeCn + 词 + _punctAfterCn
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

        // 退格：优先删后标点，再删前标点，最后删拼音
        if (wParam == VK_BACK)
        {
            if (_punctAfterLen > 0 || _punctBeforeLen > 0)
            {
                _RemoveLastPunct();
                _keyHandledInSession = true;
                return;
            }
            // 没有挂着的标点了，让它继续往下删拼音
        }

        // 再输入一个标点：追加到后标点序列（记录它当前的插入位置，显示时插回原处）
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
        {
            if (_punctAfterLen == 0)
            {
                WCHAR bufPy[128] = { 0 };
                IME_GetComposition(bufPy, ARRAYSIZE(bufPy));
                _punctAfterPos = (int)wcslen(bufPy);
            }
            _AppendPunctAfter(cn, en);
            _keyHandledInSession = true;
            return;
        }

        // 空格 / 回车 / 数字：选词（混合态候选 = 标点候选(前) + 拼音候选(后)）
        if (wParam == VK_SPACE || wParam == VK_RETURN ||
            (wParam >= '1' && wParam <= '9'))
        {
            WCHAR committed[64] = { 0 };
            bool fCommitted = false;

            // 计算在"合并候选列表"里的绝对下标
            int absIdx = 0;
            if (wParam == VK_SPACE || wParam == VK_RETURN)
                absIdx = 0;
            else
                absIdx = _pageIndex * M2C_PAGE_SIZE + (int)(wParam - '1');

            // 先看是不是标点候选（混合态下前 _mixedPunctCandCount 个是标点）
            if (_punctPending && _punctInComp && absIdx < _mixedPunctCandCount)
            {
                // 选标点：上屏对应形式（用 _BuildPunctCandidates 算出来，与候选窗保持一致）
                WCHAR pCn = 0, pEn = 0;
                PunctSide pSide = PSIDE_NONE;
                _GetMixedPunctSource(&pCn, &pEn, &pSide);
                std::vector<std::wstring> punctCands;
                _BuildPunctCandidates(pCn, pEn,
                                      _punctMode == IME_PUNCT_FULLWIDTH,
                                      punctCands);
                if (absIdx < (int)punctCands.size())
                {
                    // 标点上屏：选哪个形式就替换对应序列的第一个字符
                    // （多字符候选只用首字符，因为每次选的是单个标点的形式）
                    const WCHAR chosen = punctCands[absIdx][0];
                    if (pSide == PSIDE_BEFORE && _punctBeforeLen > 0)
                        _punctBeforeCn[0] = chosen;
                    else if (pSide == PSIDE_AFTER && _punctAfterLen > 0)
                        _punctAfterCn[0] = chosen;
                }
                // 然后走正常的"拼音选完"路径：前标点+词+后标点一起上屏
                // 但这里还没选词，先把标点的修正应用，继续让拼音选词走
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
                    int pyIdx = absIdx - _mixedPunctCandCount;
                    if (IME_SelectCandidate(pyIdx))
                    {
                        IME_GetCommitted(committed, ARRAYSIZE(committed));
                        IME_ClearCommitted();
                        fCommitted = true;
                    }
                }
            }
            else if (wParam == VK_SPACE || wParam == VK_RETURN)
            {
                // 空格/回车：跳过了标点候选，直接选拼音第 0 个
                if (IME_OnChar(wParam == VK_SPACE ? ' ' : '\r'))
                {
                    IME_GetCommitted(committed, ARRAYSIZE(committed));
                    IME_ClearCommitted();
                    fCommitted = true;
                }
            }
            else
            {
                // 拼音候选（加 offset 映射回 IME 原始下标）
                int pyIdx = absIdx - _mixedPunctCandCount;
                if (pyIdx >= 0 && IME_SelectCandidate(pyIdx))
                {
                    IME_GetCommitted(committed, ARRAYSIZE(committed));
                    IME_ClearCommitted();
                    fCommitted = true;
                }
            }
            if (fCommitted)
            {
                if (!IME_HasComposition())
                {
                    // 拼音已全部选完：前标点 + 词 + 后标点 一起上屏
                    if (_punctBeforeLen > 0)
                        _InsertTextAtSelection(ec, pContext, _punctBeforeCn, _punctBeforeLen);
                    if (committed[0])
                        _InsertTextAtSelection(ec, pContext, committed, (LONG)wcslen(committed));
                    if (_punctAfterLen > 0)
                        _InsertTextAtSelection(ec, pContext, _punctAfterCn, _punctAfterLen);
                    _ClearPunctPending();
                }
                else
                {
                    // 还有剩余拼音：有前标点时标点跟本次选中的词一起上屏（引号只包住第一个词），
                    // 否则只上屏词、后标点继续挂着
                    if (_punctBeforeLen > 0)
                    {
                        _InsertTextAtSelection(ec, pContext, _punctBeforeCn, _punctBeforeLen);
                        if (committed[0])
                            _InsertTextAtSelection(ec, pContext, committed, (LONG)wcslen(committed));
                        if (_punctAfterLen > 0)
                            _InsertTextAtSelection(ec, pContext, _punctAfterCn, _punctAfterLen);
                        _ClearPunctPending();
                    }
                    else if (committed[0])
                    {
                        _InsertTextAtSelection(ec, pContext, committed, (LONG)wcslen(committed));
                    }
                }
                _pageIndex = 0;
            }
            _keyHandledInSession = true;
            return;
        }

        // 字母等其它键：若只有后标点（先输拼音后输标点），自动提交当前词 + 后标点，
        // 然后用该键开始新的拼音；若有前标点（先输标点后输拼音），不自动提交，
        // 让字母继续走正常拼音流程
        if (_punctBeforeLen == 0)
        {
            WCHAR committed[64] = { 0 };
            if (IME_OnChar(' '))
            {
                IME_GetCommitted(committed, ARRAYSIZE(committed));
                IME_ClearCommitted();
            }
            _pageIndex = 0;
            if (!IME_HasComposition())
            {
                if (committed[0])
                    _InsertTextAtSelection(ec, pContext, committed, (LONG)wcslen(committed));
                if (_punctAfterLen > 0)
                    _InsertTextAtSelection(ec, pContext, _punctAfterCn, _punctAfterLen);
                _ClearPunctPending();
            }
            else
            {
                if (committed[0])
                    _InsertTextAtSelection(ec, pContext, committed, (LONG)wcslen(committed));
            }
            fHasComp = IME_HasComposition();
        }
        // 有前标点：不在这里 return，让按键继续往下走到正常的字母/拼音处理
    }

    // 0.1) 标点候选态（无拼音）：等待用户选择中文 / 英文标点
    // 此时所有标点都进 _punctBefore* 序列（还没有组合串，自然都是"前"标点）
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

        // 又输入一个标点：追加到前标点序列
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
        {
            _AppendPunctBefore(cn, en);
            _keyHandledInSession = true;
            return;
        }

        // 空格 / 回车 / 数字：确认标点候选
        if (wParam == VK_SPACE || wParam == VK_RETURN ||
            (wParam >= '0' && wParam <= '9'))
        {
            const WCHAR* first = (_punctMode == IME_PUNCT_FULLWIDTH) ? _punctBeforeCn : _punctBeforeEn;
            const WCHAR* second = (_punctMode == IME_PUNCT_FULLWIDTH) ? _punctBeforeEn : _punctBeforeCn;

            if (wParam == VK_SPACE || wParam == VK_RETURN || wParam == '1')
                _InsertTextAtSelection(ec, pContext, first, _punctBeforeLen);
            else if (wParam == '2')
                _InsertTextAtSelection(ec, pContext, second, _punctBeforeLen);
            else
            {
                WCHAR d = (WCHAR)wParam;
                _InsertTextAtSelection(ec, pContext, &d, 1);
            }
            _ClearPunctPending();
            _keyHandledInSession = true;
            return;
        }

        // 其它键（字母等）：用户不等标点候选、直接开始拼音。
        // 隐式确认标点：把 _punctBeforeEn 序列（用户原本输入的形式）上屏到文档，
        // 然后清空标点状态、让字母继续走拼音流程。
        // 效果等同"用户按空格选了标点再开始拼音"，但少一步按键。
        // 这样既不让标点候选和拼音候选混在一个候选框里，也不丢用户输入的标点。
        if (_punctBeforeLen > 0)
        {
            _InsertTextAtSelection(ec, pContext, _punctBeforeEn, _punctBeforeLen);
        }
        _ClearPunctPending();
        _pageIndex = 0;
        // 不要 return，让按键继续往下走到后面的字母/拼音处理逻辑
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
        WCHAR beforeBuf[128] = { 0 }, afterBuf[128] = { 0 };
        IME_GetComposition(beforeBuf, ARRAYSIZE(beforeBuf));
        if (IME_SelectCandidate(idx))
        {
            _pageIndex = 0;
            // 被消费掉的前缀里如果带标点，先按输入时的原样上屏
            IME_GetComposition(afterBuf, ARRAYSIZE(afterBuf));
            std::wstring punctPrefix = _TakeConsumedPunct(beforeBuf, afterBuf);
            if (!punctPrefix.empty())
                _InsertTextAtSelection(ec, pContext, punctPrefix.c_str(), (LONG)punctPrefix.size());

            WCHAR buf[64] = { 0 };
            IME_GetCommitted(buf, ARRAYSIZE(buf));
            IME_ClearCommitted();
            if (buf[0])
                _InsertTextAtSelection(ec, pContext, buf, (LONG)wcslen(buf));
        }
        _keyHandledInSession = true;
        return;
    }

    // 5) 标点：进入候选
    // 有拼音（fHasComp=true）→ 后标点序列，_punctInComp=true
    // 无拼音 → 前标点序列，_punctInComp=false（纯标点候选态）
    // 6) 字母 / 标点 / 空格 / 回车：交给引擎（引擎里标点也进组合串，分词器跳过标点）
    char ch = 0;
    if (wParam >= 'A' && wParam <= 'Z')
        ch = (char)(wParam - 'A' + 'a');
    else if (wParam == VK_SPACE)
        ch = ' ';
    else if (wParam == VK_RETURN)
        ch = '\r';
    else
    {
        WCHAR cn = 0, en = 0;
        if (_MapPunct(wParam, &cn, &en))
        {
            ch = (char)en;  // 用 MapPunct 映射好的正确 char
            if (ch == 0)
                ch = (char)(unsigned short)wParam;  // 兜底
        }
    }

    if (ch != 0)
    {
        WCHAR beforeBuf[128] = { 0 }, afterBuf[128] = { 0 };
        bool fHadComp = IME_HasComposition();
        if (fHadComp)
            IME_GetComposition(beforeBuf, ARRAYSIZE(beforeBuf));

        if (IME_OnChar(ch))
        {
            if (fHadComp)
            {
                // 空格提交了词：被消费掉的标点先按原样上屏
                IME_GetComposition(afterBuf, ARRAYSIZE(afterBuf));
                std::wstring punctPrefix = _TakeConsumedPunct(beforeBuf, afterBuf);
                if (!punctPrefix.empty())
                    _InsertTextAtSelection(ec, pContext, punctPrefix.c_str(), (LONG)punctPrefix.size());
            }

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
        // 纯标点候选态（无拼音）
        comp = _punctBeforeEn;
        if (_punctMode == IME_PUNCT_FULLWIDTH)
        {
            items.emplace_back(_punctBeforeCn, 0);
            items.emplace_back(_punctBeforeEn, 0);
        }
        else
        {
            items.emplace_back(_punctBeforeEn, 0);
            items.emplace_back(_punctBeforeCn, 0);
        }
    }
    else
    {
        // 组合串里标点与拼音共存：候选窗直接显示组合串原文
        WCHAR bufComp[128] = { 0 };
        IME_GetComposition(bufComp, ARRAYSIZE(bufComp));
        std::wstring py(bufComp);
        comp = py;
        _mixedPunctCandCount = 0;

        // 组合串以标点开头（从头选词）→ 先列出该标点的各种写法
        WCHAR firstPunct = 0;
        std::vector<std::wstring> punctCands;
        if (_StartsWithPunct(py, &firstPunct))
        {
            WCHAR cn = 0;
            if (_MapPunctChar(firstPunct, &cn))
                _mixedPunctCandCount = _BuildPunctCandidates(cn, firstPunct,
                                            _punctMode == IME_PUNCT_FULLWIDTH,
                                            punctCands);
            for (int i = 0; i < _mixedPunctCandCount; ++i)
                items.emplace_back(punctCands[i], 0);
        }
        else
        {
            // 正常拼音候选
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

    // 组合串以标点开头（从头选词）：点击候选 = 把首位标点换成点中的写法并上屏
    {
        WCHAR compBuf[128] = { 0 };
        if (IME_HasComposition())
            IME_GetComposition(compBuf, ARRAYSIZE(compBuf));
        std::wstring compNow(compBuf);
        WCHAR firstPunct = 0;
        if (_StartsWithPunct(compNow, &firstPunct))
        {
            WCHAR pCn = 0;
            std::vector<std::wstring> punctCands;
            int nCand = 0;
            if (_MapPunctChar(firstPunct, &pCn))
                nCand = _BuildPunctCandidates(pCn, firstPunct,
                                              _punctMode == IME_PUNCT_FULLWIDTH,
                                              punctCands);
            if (itemIndexOnPage < 0 || itemIndexOnPage >= nCand) return;

            std::wstring chosen = punctCands[itemIndexOnPage];

            ITfDocumentMgr* pDocMgr = nullptr;
            if (FAILED(_pThreadMgr->GetFocus(&pDocMgr)) || !pDocMgr) return;
            ITfContext* pContext = nullptr;
            if (SUCCEEDED(pDocMgr->GetBase(&pContext)) && pContext)
            {
                CEditSession session([this, pContext, chosen](TfEditCookie ec) -> HRESULT
                {
                    _InsertTextAtSelection(ec, pContext, chosen.c_str(), 1);
                    IME_DeleteCompositionFront(1);
                    return S_OK;
                });
                HRESULT hrSession = E_FAIL;
                pContext->RequestEditSession(_tfClientId, &session, TF_ES_READWRITE | TF_ES_SYNC, &hrSession);
                pContext->Release();
            }
            pDocMgr->Release();
            _pageIndex = 0;
            if (_pCandidateWnd)
            {
                if (IME_HasComposition())
                    _UpdateCandidateWindow(nullptr, nullptr);
                else
                    _pCandidateWnd->_Hide();
            }
            return;
        }
    }

    // 合并候选列表里的绝对下标
    int absIdx = _pageIndex * M2C_PAGE_SIZE + itemIndexOnPage;

    int pyIdx = absIdx;
    if (pyIdx < 0) pyIdx = 0;
    if (IME_GetCandidateCount() == 0 || pyIdx >= IME_GetCandidateCount())
        return;

    ITfDocumentMgr* pDocMgr = nullptr;
    if (FAILED(_pThreadMgr->GetFocus(&pDocMgr)) || !pDocMgr) return;

    ITfContext* pContext = nullptr;
    RECT caretRect = {};
    bool hasCaretRect = false;
    if (SUCCEEDED(pDocMgr->GetBase(&pContext)) && pContext)
    {
        CEditSession session([this, pContext, pyIdx, &caretRect, &hasCaretRect](TfEditCookie ec) -> HRESULT
        {
            WCHAR beforeBuf[128] = { 0 }, afterBuf[128] = { 0 };
            IME_GetComposition(beforeBuf, ARRAYSIZE(beforeBuf));
            if (IME_SelectCandidate(pyIdx))
            {
                WCHAR buf[64] = { 0 };
                IME_GetCommitted(buf, ARRAYSIZE(buf));
                IME_ClearCommitted();

                // 被消费掉的前缀里如果带标点，先按输入时的原样上屏
                IME_GetComposition(afterBuf, ARRAYSIZE(afterBuf));
                std::wstring punctPrefix = _TakeConsumedPunct(beforeBuf, afterBuf);
                if (!punctPrefix.empty())
                    _InsertTextAtSelection(ec, pContext, punctPrefix.c_str(), (LONG)punctPrefix.size());

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
    // 选词后刷新候选窗：有剩余拼音则继续显示候选，否则隐藏
    if (_pCandidateWnd)
    {
        if (IME_HasComposition())
            _UpdateCandidateWindow(nullptr, hasCaretRect ? &caretRect : nullptr);
        else
            _pCandidateWnd->_Hide();
    }
}

// TextService.cpp —— 激活/停用、Sink 管理、模式切换
#include "Globals.h"
#include "TextService.h"
#include "CandidateWindow.h"
#include "LangBarButton.h"
#include "LangBarPunctButton.h"
#include "StatusBar.h"

#define M2C_REG_KEY     L"Software\\M2CIME"
#define M2C_REG_MODE    L"Mode"
#define M2C_REG_PUNCT   L"PunctMode"

CTextService::CTextService()
    : _cRef(1)
    , _pThreadMgr(nullptr)
    , _tfClientId(TF_CLIENTID_NULL)
    , _dwThreadFocusSinkCookie(TF_INVALID_COOKIE)
    , _dwActiveProfileSinkCookie(TF_INVALID_COOKIE)
    , _pCandidateWnd(nullptr)
    , _pLangBarButton(nullptr)
    , _pLangBarPunctButton(nullptr)
    , _pStatusBar(nullptr)
    , _mode(IME_MODE_MANDARIN)
    , _lastChineseMode(IME_MODE_MANDARIN)
    , _punctMode(IME_PUNCT_HALFWIDTH)
    , _shiftPressed(false)
    , _keyHandledInSession(false)
    , _pageIndex(0)
    , _hasCaretRect(false)
    , _punctPending(false)
    , _punctInComp(false)
    , _punctBeforeLen(0)
    , _punctAfterLen(0)
    , _punctAfterPos(0)
    , _mixedPunctCandCount(0)
{
    DllAddRef();
    IME_Init();
    _LoadModeFromRegistry();
    _LoadPunctModeFromRegistry();
    IME_SetMode(_mode);
    IME_SetPunctMode(_punctMode);
    SetRectEmpty(&_caretRect);
    _punctBeforeCn[0] = 0;
    _punctBeforeEn[0] = 0;
    _punctAfterCn[0] = 0;
    _punctAfterEn[0] = 0;
}

CTextService::~CTextService()
{
    DllRelease();
}

// ================= IUnknown =================

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** ppvObj)
{
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor) ||
        IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppvObj = (ITfTextInputProcessorEx*)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppvObj = (ITfKeyEventSink*)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
        *ppvObj = (ITfThreadFocusSink*)this;
    }
    else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink))
    {
        *ppvObj = (ITfActiveLanguageProfileNotifySink*)this;
    }
    else
    {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef()
{
    return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) CTextService::Release()
{
    LONG c = InterlockedDecrement(&_cRef);
    if (c == 0) delete this;
    return c;
}

// ================= Activate / Deactivate =================

STDMETHODIMP CTextService::Activate(ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
    return ActivateEx(pThreadMgr, tfClientId, 0);
}

STDMETHODIMP CTextService::ActivateEx(ITfThreadMgr* pThreadMgr, TfClientId tfClientId, DWORD /*dwFlags*/)
{
    _pThreadMgr = pThreadMgr;
    if (_pThreadMgr) _pThreadMgr->AddRef();
    _tfClientId = tfClientId;

    // 安装键盘事件 sink
    ITfKeystrokeMgr* pKeyMgr = nullptr;
    if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeyMgr)))
    {
        pKeyMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this, TRUE);
        pKeyMgr->Release();
    }

    // 线程焦点 sink（焦点变化时隐藏候选窗）与输入法切换 sink（状态条跟随输入法）
    ITfSource* pSource = nullptr;
    if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource)))
    {
        pSource->AdviseSink(IID_ITfThreadFocusSink, (ITfThreadFocusSink*)this, &_dwThreadFocusSinkCookie);
        pSource->AdviseSink(IID_ITfActiveLanguageProfileNotifySink,
                            (ITfActiveLanguageProfileNotifySink*)this, &_dwActiveProfileSinkCookie);
        pSource->Release();
    }

    // 创建候选窗（隐藏状态）
    if (_pCandidateWnd == nullptr)
        _pCandidateWnd = new CCandidateWindow(this);

    // 语言栏模式按钮
    ITfLangBarItemMgr* pLbiMgr = nullptr;
    if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLbiMgr)))
    {
        _pLangBarButton = new CLangBarButton(this);
        pLbiMgr->AddItem(_pLangBarButton);

        // 全角/半角标点切换按钮
        _pLangBarPunctButton = new CLangBarPunctButton(this);
        pLbiMgr->AddItem(_pLangBarPunctButton);

        pLbiMgr->Release();
    }

    // 自带状态条（英/普/粤 切换入口）。TIP 会被加载进所有 TSF 进程（与当前输入法无关），
    // 因此不能激活即显示，必须按「本输入法是否为当前线程活动输入法」判定；
    // 之后由线程焦点变化 / 输入法切换通知持续同步显隐。
    if (_pStatusBar == nullptr)
        _pStatusBar = new CStatusBar(this);
    _SyncStatusBarVisibility();

    return S_OK;
}

STDMETHODIMP CTextService::Deactivate()
{
    // 销毁状态条
    if (_pStatusBar)
    {
        _pStatusBar->_Destroy();
        delete _pStatusBar;
        _pStatusBar = nullptr;
    }

    // 移除语言栏按钮（模式切换 + 全角/半角切换）
    if (_pLangBarButton || _pLangBarPunctButton)
    {
        ITfLangBarItemMgr* pLbiMgr = nullptr;
        if (_pThreadMgr && SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void**)&pLbiMgr)))
        {
            if (_pLangBarButton)
            {
                pLbiMgr->RemoveItem(_pLangBarButton);
                _pLangBarButton->Release();
                _pLangBarButton = nullptr;
            }
            if (_pLangBarPunctButton)
            {
                pLbiMgr->RemoveItem(_pLangBarPunctButton);
                _pLangBarPunctButton->Release();
                _pLangBarPunctButton = nullptr;
            }
            pLbiMgr->Release();
        }
        else
        {
            // 即使获取不到 LangBarItemMgr，也要释放按钮
            if (_pLangBarButton) { _pLangBarButton->Release(); _pLangBarButton = nullptr; }
            if (_pLangBarPunctButton) { _pLangBarPunctButton->Release(); _pLangBarPunctButton = nullptr; }
        }
    }

    // 销毁候选窗
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_Destroy();
        _pCandidateWnd->Release();
        _pCandidateWnd = nullptr;
    }

    // 卸载 sinks
    if (_pThreadMgr)
    {
        ITfKeystrokeMgr* pKeyMgr = nullptr;
        if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeyMgr)))
        {
            pKeyMgr->UnadviseKeyEventSink(_tfClientId);
            pKeyMgr->Release();
        }
        ITfSource* pSource = nullptr;
        if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource)))
        {
            if (_dwThreadFocusSinkCookie != TF_INVALID_COOKIE)
                pSource->UnadviseSink(_dwThreadFocusSinkCookie);
            if (_dwActiveProfileSinkCookie != TF_INVALID_COOKIE)
                pSource->UnadviseSink(_dwActiveProfileSinkCookie);
            pSource->Release();
        }
        _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
        _dwActiveProfileSinkCookie = TF_INVALID_COOKIE;
        _pThreadMgr->Release();
        _pThreadMgr = nullptr;
    }
    _tfClientId = TF_CLIENTID_NULL;
    return S_OK;
}

// ================= ITfThreadFocusSink =================

STDMETHODIMP CTextService::OnSetThreadFocus()
{
    // 本线程重新获得焦点：当前输入法仍是本输入法时显示状态条
    _SyncStatusBarVisibility();
    return S_OK;
}

STDMETHODIMP CTextService::OnKillThreadFocus()
{
    // 焦点离开：清空组合串并隐藏候选窗、状态条
    IME_ClearComposition();
    _ClearPunctPending();
    _pageIndex = 0;
    if (_pCandidateWnd) _pCandidateWnd->_Hide();
    if (_pStatusBar) _pStatusBar->_Hide();
    return S_OK;
}

STDMETHODIMP CTextService::OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated)
{
    // 任何输入法激活/停用都可能影响状态条显隐：
    // 本输入法被停用 → 立即隐藏（此时活动输入法可能尚未切换，不能靠查询判定）；
    // 其它情况（本输入法激活、其它输入法激活/停用）→ 查询当前活动输入法重新判定。
    if (!_pStatusBar) return S_OK;

    BOOL fMine = IsEqualCLSID(clsid, CLSID_M2CTextService) && IsEqualGUID(guidProfile, GUID_M2CProfile);
    if (fMine && !fActivated)
        _pStatusBar->_Hide();
    else
        _SyncStatusBarVisibility();
    return S_OK;
}

// ================= 模式管理 =================

void CTextService::SetMode(ImeInputMode mode, bool fSaveToRegistry)
{
    _mode = mode;
    if (mode != IME_MODE_ENGLISH)
        _lastChineseMode = mode;

    IME_SetMode(mode);
    _ClearPunctPending();
    _pageIndex = 0;
    if (_pCandidateWnd) _pCandidateWnd->_Hide();

    if (fSaveToRegistry)
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, M2C_REG_KEY, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            DWORD v = (DWORD)mode;
            RegSetValueExW(hKey, M2C_REG_MODE, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
            RegCloseKey(hKey);
        }
    }
    _UpdateLangBarButton();
}

void CTextService::_LoadModeFromRegistry()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, M2C_REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type = 0, size = sizeof(DWORD), v = IME_MODE_MANDARIN;
        if (RegQueryValueExW(hKey, M2C_REG_MODE, nullptr, &type, (BYTE*)&v, &size) == ERROR_SUCCESS
            && type == REG_DWORD && v <= IME_MODE_CANTONESE)
        {
            _mode = (ImeInputMode)v;
            if (_mode != IME_MODE_ENGLISH)
                _lastChineseMode = _mode;
        }
        RegCloseKey(hKey);
    }
}

void CTextService::_LoadPunctModeFromRegistry()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, M2C_REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type = 0, size = sizeof(DWORD), v = IME_PUNCT_HALFWIDTH;
        if (RegQueryValueExW(hKey, M2C_REG_PUNCT, nullptr, &type, (BYTE*)&v, &size) == ERROR_SUCCESS
            && type == REG_DWORD && v <= IME_PUNCT_FULLWIDTH)
        {
            _punctMode = (ImePunctMode)v;
        }
        RegCloseKey(hKey);
    }
}

void CTextService::SetPunctMode(ImePunctMode mode, bool fSaveToRegistry)
{
    _punctMode = mode;
    IME_SetPunctMode(mode);

    if (fSaveToRegistry)
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, M2C_REG_KEY, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            DWORD v = (DWORD)mode;
            RegSetValueExW(hKey, M2C_REG_PUNCT, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
            RegCloseKey(hKey);
        }
    }
    _UpdateLangBarPunctButton();
}

void CTextService::TogglePunctMode()
{
    SetPunctMode(_punctMode == IME_PUNCT_FULLWIDTH ? IME_PUNCT_HALFWIDTH : IME_PUNCT_FULLWIDTH);
}

void CTextService::_UpdateLangBarButton()
{
    if (_pLangBarButton)
        _pLangBarButton->_NotifyUpdate();
    if (_pStatusBar)
        _pStatusBar->_Update();
}

void CTextService::_UpdateLangBarPunctButton()
{
    if (_pLangBarPunctButton)
        _pLangBarPunctButton->_NotifyUpdate();
    if (_pStatusBar)
        _pStatusBar->_Update();
}

// 查询本线程当前激活的键盘输入法是否为本输入法。
// 注意：ITfInputProcessorProfileMgr 不能从 ITfThreadMgr 查询（会返回 E_NOINTERFACE），
// 必须通过 CLSID_TF_InputProcessorProfiles 对象获取。
// 查询失败时返回 FALSE（宁可隐藏，避免浮在其它输入法上）。
BOOL CTextService::_IsM2CProfileActive()
{
    ITfInputProcessorProfiles* pProfiles = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfiles, (void**)&pProfiles))
        || pProfiles == nullptr)
        return FALSE;

    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    if (FAILED(pProfiles->QueryInterface(IID_ITfInputProcessorProfileMgr, (void**)&pProfileMgr))
        || pProfileMgr == nullptr)
    {
        pProfiles->Release();
        return FALSE;
    }

    TF_INPUTPROCESSORPROFILE profile = {};
    BOOL fActive = FALSE;
    if (SUCCEEDED(pProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)))
    {
        fActive = IsEqualCLSID(profile.clsid, CLSID_M2CTextService)
               && IsEqualGUID(profile.guidProfile, GUID_M2CProfile);
    }
    pProfileMgr->Release();
    pProfiles->Release();
    return fActive;
}

void CTextService::_SyncStatusBarVisibility()
{
    if (!_pStatusBar) return;

    if (_IsM2CProfileActive())
        _pStatusBar->_Show();
    else
        _pStatusBar->_Hide();
}

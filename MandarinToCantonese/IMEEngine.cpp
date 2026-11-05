#include "pch.h"
#include "IMEEngine.h"
#include "Dictionary.h"

#include <string>
#include <vector>
#include <cctype>

namespace
{
    // ===== 全局引擎状态 =====
    ImeInputMode g_mode = IME_MODE_MANDARIN;
    ImePunctMode g_punctMode = IME_PUNCT_HALFWIDTH;  // 默认半角标点

    std::string          g_composition;     // 当前拼音串（小写）
    std::vector<DictEntry> g_candidates;    // 当前候选词列表
    std::wstring         g_committed;       // 最近一次提交的文本

    bool g_initialized = false;

    void EnsureInit()
    {
        if (g_initialized) return;
        Dictionary::Instance().EnsureLoaded();
        g_initialized = true;
    }

    // 根据当前组合串重新生成候选词
    void RefreshCandidates()
    {
        g_candidates.clear();
        if (g_composition.empty()) return;

        // 直接把完整的组合串交给词典：
        //  - 整串都是拼音时按前缀 / 完全匹配（如 ni、nihao）；
        //  - 只输入了声母时按声母匹配（如 nh）；
        //  - 混拼时逐音节匹配（如 nih = ni + h、nhao = n + hao，都能列出「你好」）。
        int mode = (g_mode == IME_MODE_CANTONESE) ? 1 : 0;
        // 不限制候选数量，全部放入候选列表，由前端翻页展示
        g_candidates = Dictionary::Instance().GetCandidates(g_composition, mode, g_composition);
    }

    // 提交一个候选词
    bool CommitCandidate(int index)
    {
        if (index < 0 || index >= (int)g_candidates.size()) return false;

        const DictEntry& chosen = g_candidates[index];
        g_committed = chosen.word;

        if (chosen.viaInitials)
        {
            // 声母简拼匹配（如 nh → 你好）：整个组合串都是声母，全部消耗
            g_composition.clear();
        }
        else
        {
            // 连续选词：只截掉候选拼音与组合串相同的公共前缀部分。
            //  - 输入 doubao 时选「豆」(dou) → 余下 bao，刷新候选继续选词；
            //  - 只输入声母 n 时选「你」(ni) → 公共前缀 n 被消耗，组合串清空。
            size_t matchLen = 0;
            while (matchLen < chosen.pinyin.size() &&
                   matchLen < g_composition.size() &&
                   chosen.pinyin[matchLen] == g_composition[matchLen])
            {
                ++matchLen;
            }

            g_composition.erase(0, matchLen);
        }

        g_candidates.clear();
        if (!g_composition.empty())
            RefreshCandidates();
        return true;
    }

    // 直接提交一段文本（英文模式或原始拼音）
    void CommitRaw(const std::wstring& text)
    {
        g_committed = text;
    }
}

// ===== 导出函数实现 =====

void IME_Init()
{
    EnsureInit();
}

void IME_SetMode(ImeInputMode mode)
{
    if (g_mode != mode)
    {
        g_mode = mode;
        // 切换模式时清空当前组合，避免候选与新模式不一致
        g_composition.clear();
        g_candidates.clear();
        // 注意：不清空 g_committed，前端仍可取走上一次提交
    }
}

ImeInputMode IME_GetMode()
{
    return g_mode;
}

void IME_SetPunctMode(ImePunctMode mode)
{
    g_punctMode = mode;
}

ImePunctMode IME_GetPunctMode()
{
    return g_punctMode;
}

// 半角→全角标点映射表（只覆盖常用标点，空格除外）
static bool MapHalfToFullPunct(char half, wchar_t* pFull)
{
    struct PunctMap { char half; wchar_t full; };
    static const PunctMap kMap[] = {
        { ',', 0xFF0C },   // ，
        { '.', 0x3002 },   // 。
        { '!', 0xFF01 },   // ！
        { '?', 0xFF1F },   // ？
        { ';', 0xFF1B },   // ；
        { ':', 0xFF1A },   // ：
        { '`', 0xFF40 },   // ｀
        { '\'', 0xFF07 },  // ＇
        { '"', 0xFF02 },   // ＂
        { '(', 0xFF08 },   // （
        { ')', 0xFF09 },   // ）
        { '[', 0x3010 },   // 【
        { ']', 0x3011 },   // 】
        { '{', 0xFF5B },   // ｛
        { '}', 0xFF5D },   // ｝
        { '<', 0x300A },   // 《
        { '>', 0x300B },   // 》
        { '/', 0xFF0F },   // ／
        { '\\', 0xFF3C },  // ＼
        { '-', 0xFF0D },   // －
        { '_', 0xFF3F },   // ＿
        { '=', 0xFF1D },   // ＝
        { '+', 0xFF0B },   // ＋
        { '*', 0xFF0A },   // ＊
        { '&', 0xFF06 },   // ＆
        { '%', 0xFF05 },   // ％
        { '$', 0xFF04 },   // ＄
        { '#', 0xFF03 },   // ＃
        { '@', 0xFF20 },   // ＠
        { '~', 0xFF5E },   // ～
        { '^', 0xFF3E },   // ＾
        { '|', 0xFF5C },   // ｜
    };
    for (const auto& m : kMap)
    {
        if (m.half == half)
        {
            *pFull = m.full;
            return true;
        }
    }
    return false;
}

wchar_t IME_ConvertPunctChar(char ch)
{
    wchar_t wc = (wchar_t)(unsigned char)ch;
    if (g_punctMode == IME_PUNCT_FULLWIDTH)
    {
        wchar_t full = 0;
        if (MapHalfToFullPunct(ch, &full))
            wc = full;
    }
    return wc;
}

bool IME_OnChar(char ch)
{
    EnsureInit();

    // 英文模式：字母、数字、空格直接提交；标点根据全角/半角模式转换后提交
    if (g_mode == IME_MODE_ENGLISH)
    {
        if (isalnum((unsigned char)ch) || ch == ' ')
        {
            wchar_t wc = (wchar_t)(unsigned char)ch;
            CommitRaw(std::wstring(1, wc));
            return true;
        }
        // 标点符号：根据全角/半角模式转换
        if (ch == ',' || ch == '.' || ch == '!' || ch == '?' ||
            ch == ';' || ch == ':' || ch == '\'' || ch == '"' ||
            ch == '-' || ch == '_' || ch == '(' || ch == ')' ||
            ch == '/' || ch == '\\' || ch == '`' || ch == '[' ||
            ch == ']' || ch == '{' || ch == '}' || ch == '<' ||
            ch == '>' || ch == '=' || ch == '+' || ch == '*' ||
            ch == '&' || ch == '%' || ch == '$' || ch == '#' ||
            ch == '@' || ch == '~' || ch == '^' || ch == '|')
        {
            wchar_t wc = IME_ConvertPunctChar(ch);
            CommitRaw(std::wstring(1, wc));
            return true;
        }
        return false;
    }

    // 普通话 / 粤语模式
    if (isalpha((unsigned char)ch))
    {
        // 字母：追加到组合串
        g_composition.push_back((char)tolower((unsigned char)ch));
        RefreshCandidates();
        return true;
    }

    if (ch == ' ')
    {
        // 空格：有组合串时提交第一个候选，否则提交空格
        if (!g_composition.empty())
        {
            return CommitCandidate(0);
        }
        else
        {
            CommitRaw(L" ");
            return true;
        }
    }

    if (ch >= '1' && ch <= '9')
    {
        // 数字键 1-9：选择对应候选
        int idx = ch - '1';
        if (idx < (int)g_candidates.size())
        {
            return CommitCandidate(idx);
        }
        // 没有对应候选，不消费该键（让前端输入数字）
        return false;
    }

    if (ch == '\r' || ch == '\n')
    {
        // 回车：把当前组合串作为原始拼音提交
        if (!g_composition.empty())
        {
            std::wstring raw(g_composition.begin(), g_composition.end());
            CommitRaw(raw);
            g_composition.clear();
            g_candidates.clear();
            return true;
        }
        return false;
    }

    // 其它标点：有组合串时先不处理，交给前端；无组合串时直接提交
    if (g_composition.empty())
    {
        wchar_t wc = (wchar_t)(unsigned char)ch;
        CommitRaw(std::wstring(1, wc));
        return true;
    }
    return false;
}

bool IME_OnBackspace()
{
    if (!g_composition.empty())
    {
        g_composition.pop_back();
        RefreshCandidates();
        return true;
    }
    return false;
}

int IME_GetComposition(wchar_t* buf, int bufLen)
{
    if (!buf || bufLen <= 0) return 0;
    // 组合串是 ASCII 拼音，直接转换
    int n = (int)g_composition.size();
    if (n >= bufLen) n = bufLen - 1;
    for (int i = 0; i < n; ++i)
        buf[i] = (wchar_t)(unsigned char)g_composition[i];
    buf[n] = L'\0';
    return n;
}

int IME_GetCandidateCount()
{
    return (int)g_candidates.size();
}

int IME_GetCandidate(int index, wchar_t* buf, int bufLen)
{
    if (!buf || bufLen <= 0 || index < 0 || index >= (int)g_candidates.size())
    {
        if (buf && bufLen > 0) buf[0] = L'\0';
        return 0;
    }
    const std::wstring& w = g_candidates[index].word;
    int n = (int)w.size();
    if (n >= bufLen) n = bufLen - 1;
    for (int i = 0; i < n; ++i) buf[i] = w[i];
    buf[n] = L'\0';
    return n;
}

int IME_GetCandidateSource(int index)
{
    if (index < 0 || index >= (int)g_candidates.size()) return -1;
    return (g_candidates[index].source == LangSource::Cantonese) ? 1 : 0;
}

bool IME_SelectCandidate(int index)
{
    EnsureInit();
    return CommitCandidate(index);
}

bool IME_CommitFirst()
{
    EnsureInit();
    return CommitCandidate(0);
}

int IME_GetCommitted(wchar_t* buf, int bufLen)
{
    if (!buf || bufLen <= 0) return 0;
    int n = (int)g_committed.size();
    if (n >= bufLen) n = bufLen - 1;
    for (int i = 0; i < n; ++i) buf[i] = g_committed[i];
    buf[n] = L'\0';
    return n;
}

void IME_ClearCommitted()
{
    g_committed.clear();
}

void IME_ClearComposition()
{
    g_composition.clear();
    g_candidates.clear();
}

bool IME_HasComposition()
{
    return !g_composition.empty();
}

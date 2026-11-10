#include "pch.h"
#include "IMEEngine.h"
#include "Dictionary.h"
#include "PinyinSegmenter.h"

#include <string>
#include <vector>
#include <cctype>
#include <cstring>

namespace
{
    // ===== 全局引擎状态 =====
    ImeInputMode g_mode = IME_MODE_MANDARIN;
    ImePunctMode g_punctMode = IME_PUNCT_HALFWIDTH;  // 默认半角标点
    ImeChineseVariant g_chineseVariant = IME_VARIANT_SIMPLIFIED;  // 默认简体中文
    ImePinyinMode g_pinyinMode = IME_PINYIN_FULL;     // 默认全拼

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

    // 旧词库没有 word_trad 列时的兜底：调用 Windows 内置的繁简映射。
    // 这只是逐字映射（不能处理「头发 -> 頭髮」这类词组分化），
    // 正常安装的 dicts\m2c_dict.db 已包含由 OpenCC 预生成的 word_trad，
    // 不会走到这里。
    std::wstring TraditionalFallback(const std::wstring& text)
    {
        if (text.empty()) return text;

        int n = LCMapStringEx(L"zh-TW", LCMAP_TRADITIONAL_CHINESE,
                              text.c_str(), (int)text.size(),
                              nullptr, 0, nullptr, nullptr, 0);
        if (n <= 0) return text;

        std::wstring out((size_t)n, L'\0');
        int written = LCMapStringEx(L"zh-TW", LCMAP_TRADITIONAL_CHINESE,
                                    text.c_str(), (int)text.size(),
                                    &out[0], n, nullptr, nullptr, 0);
        if (written <= 0) return text;
        out.resize((size_t)written);
        return out;
    }

    // ===== 微软双拼键位映射与解码 =====
    // 每个汉字用「声母键 + 韵母键」两键输入。
    // 声母：b p m f d t n l g k h j q x z c s r y w 与键位一一对应；
    //       zh→v, ch→i, sh→u；零声母用 o 作占位，再跟韵母键。
    // 韵母分配（微软双拼方案）：
    //   A=a  B=ou  C=iao  D=iang/uang  E=e  F=en  G=eng  H=ang  I=i  J=an
    //   K=ao  L=ai  M=ian  N=in  O=o/uo  P=un  Q=iu  R=uan/er  S=iong/ong
    //   T=ue  U=u  V=ue/ui  W=ua/ia  X=ie  Y=uai/ü  Z=ei  ;=ing

    // 取某键对应的声母（空串表示该键无声母：a/e/o 及 ;）
    static const char* ShuangpinInitial(char key)
    {
        switch (key)
        {
        case 'b': return "b"; case 'p': return "p"; case 'm': return "m"; case 'f': return "f";
        case 'd': return "d"; case 't': return "t"; case 'n': return "n"; case 'l': return "l";
        case 'g': return "g"; case 'k': return "k"; case 'h': return "h";
        case 'j': return "j"; case 'q': return "q"; case 'x': return "x";
        case 'z': return "z"; case 'c': return "c"; case 's': return "s";
        case 'r': return "r"; case 'y': return "y"; case 'w': return "w";
        case 'v': return "zh"; case 'i': return "ch"; case 'u': return "sh";
        default:  return "";   // a, e, o, ;
        }
    }

    // 取某键对应的韵母列表（可能有多个，逐个尝试取合法音节）
    static void ShuangpinFinals(char key, std::vector<std::string>& out)
    {
        out.clear();
        switch (key)
        {
        case 'a': out.push_back("a"); break;
        case 'b': out.push_back("ou"); break;
        case 'c': out.push_back("iao"); break;
        case 'd': out.push_back("iang"); out.push_back("uang"); break;
        case 'e': out.push_back("e"); break;
        case 'f': out.push_back("en"); break;
        case 'g': out.push_back("eng"); break;
        case 'h': out.push_back("ang"); break;
        case 'i': out.push_back("i"); break;
        case 'j': out.push_back("an"); break;
        case 'k': out.push_back("ao"); break;
        case 'l': out.push_back("ai"); break;
        case 'm': out.push_back("ian"); break;
        case 'n': out.push_back("in"); break;
        case 'o': out.push_back("o"); out.push_back("uo"); break;
        case 'p': out.push_back("un"); break;
        case 'q': out.push_back("iu"); break;
        case 'r': out.push_back("uan"); out.push_back("er"); break;
        case 's': out.push_back("iong"); out.push_back("ong"); break;
        case 't': out.push_back("ue"); break;
        case 'u': out.push_back("u"); break;
        case 'v': out.push_back("ue"); out.push_back("ui"); break;
        case 'w': out.push_back("ua"); out.push_back("ia"); break;
        case 'x': out.push_back("ie"); break;
        case 'y': out.push_back("uai"); out.push_back("v"); break;  // v = ü
        case 'z': out.push_back("ei"); break;
        case ';': out.push_back("ing"); break;
        }
    }

    // 声母 + 韵母 → 拼音串；处理 ü 的写法：j/q/x/y 后 ü 写 u，n/l 后写 v
    static std::string CombineInitialFinal(const std::string& initial, const std::string& finalStr)
    {
        if (finalStr == "v")
        {
            if (initial == "j" || initial == "q" || initial == "x" || initial == "y")
                return initial + "u";
            return initial + "v";
        }
        return initial + finalStr;
    }

    // 解码一对双拼键（声母键 k1 + 韵母键 k2）为一个拼音音节。
    // 若 k1 == 'o' 视为零声母，音节取 k2 的韵母（须能独立成音节）。
    static std::string ShuangpinDecodePair(char k1, char k2)
    {
        std::vector<std::string> finals;
        ShuangpinFinals(k2, finals);

        if (k1 == 'o')
        {
            // 零声母：韵母本身须为合法音节（a/ai/an/ang/ao/e/ei/en/er/o/ou）
            for (const auto& f : finals)
                if (PinyinSegmenter::IsValidSyllable(f))
                    return f;
            return finals.empty() ? std::string() : finals[0];
        }

        std::string initial = ShuangpinInitial(k1);
        for (const auto& f : finals)
        {
            std::string cand = CombineInitialFinal(initial, f);
            if (PinyinSegmenter::IsValidSyllable(cand))
                return cand;
        }
        // 无合法组合时取第一个韵母作回退（用于未完整输入时的候选提示）
        return finals.empty() ? initial : CombineInitialFinal(initial, finals[0]);
    }

    // 判断一个字符是否为双拼有效按键（a-z 或 ;）
    static bool IsShuangpinKey(char c)
    {
        return isalpha((unsigned char)c) || c == ';';
    }

    // 把双拼原始按键串解码为完整拼音串（供词典查询）。
    // 标点等非双拼键会被跳过；末尾不足一键的残余按声母处理。
    static std::string ShuangpinDecode(const std::string& raw)
    {
        std::string decoded;
        size_t i = 0;
        while (i < raw.size())
        {
            char c = raw[i];
            if (!IsShuangpinKey(c)) { ++i; continue; }

            if (i + 1 >= raw.size() || !IsShuangpinKey(raw[i + 1]))
            {
                // 孤键：输出其声母（零声母键输出空串）
                decoded += ShuangpinInitial(c);
                ++i;
                continue;
            }
            decoded += ShuangpinDecodePair(c, raw[i + 1]);
            i += 2;
        }
        return decoded;
    }

    // 双拼音节段：记录该音节消耗的原始按键数与解码后的拼音字母数
    struct ShuangpinSeg
    {
        size_t rawKeys;     // 原始按键数（1 或 2）
        size_t decodedLen;  // 解码出的拼音字母数
    };

    // 把原始按键串切分为双拼音节段（标点跳过，不计入）
    static std::vector<ShuangpinSeg> ShuangpinSegments(const std::string& raw)
    {
        std::vector<ShuangpinSeg> segs;
        size_t i = 0;
        while (i < raw.size())
        {
            char c = raw[i];
            if (!IsShuangpinKey(c)) { ++i; continue; }

            if (i + 1 >= raw.size() || !IsShuangpinKey(raw[i + 1]))
            {
                segs.push_back({ 1, std::strlen(ShuangpinInitial(c)) });
                ++i;
                continue;
            }
            std::string syl = ShuangpinDecodePair(c, raw[i + 1]);
            segs.push_back({ 2, syl.size() });
            i += 2;
        }
        return segs;
    }

    // 给定需消耗的解码拼音字母数，换算为应从原始组合串头部删除的按键数。
    // 若消耗落在某个音节中间，则把该音节整段一起删掉（双拼不能半字删）。
    static size_t ShuangpinRawKeysForDecodedLen(const std::string& raw, size_t decodedConsume)
    {
        auto segs = ShuangpinSegments(raw);
        size_t decAcc = 0, rawAcc = 0;
        for (const auto& s : segs)
        {
            if (decAcc >= decodedConsume) break;
            if (decAcc + s.decodedLen <= decodedConsume)
            {
                decAcc += s.decodedLen;
                rawAcc += s.rawKeys;
            }
            else
            {
                rawAcc += s.rawKeys;   // 落在音节中间：整段删掉
                break;
            }
        }
        return rawAcc;
    }

    // 从组合串头部删除 nKeys 个双拼按键（标点随按键一并删除，行为同 ConsumeLetters）
    static void ConsumeShuangpinKeys(std::string& s, size_t nKeys)
    {
        size_t consumed = 0;
        size_t keysSeen = 0;
        for (size_t i = 0; i < s.size() && keysSeen < nKeys; ++i)
        {
            ++consumed;
            if (IsShuangpinKey(s[i])) ++keysSeen;
        }
        s.erase(0, consumed);
    }

    // 根据当前组合串重新生成候选词
    void RefreshCandidates()
    {
        g_candidates.clear();
        if (g_composition.empty()) return;

        // 组合串里可能混有标点（如 "nin"shizuncheng），词典只认拼音，
        // 所以先用过滤掉标点/数字的纯拼音串来查候选
        std::string purePinyin;
        if (g_pinyinMode == IME_PINYIN_DOUBLE)
        {
            // 双拼：把原始按键串解码为完整拼音后再查词典
            purePinyin = ShuangpinDecode(g_composition);
        }
        else
        {
            purePinyin.reserve(g_composition.size());
            for (char c : g_composition)
                if (isalpha((unsigned char)c))
                    purePinyin.push_back((char)tolower((unsigned char)c));
        }
        if (purePinyin.empty()) return;  // 全是标点，不生成候选

        int mode = (g_mode == IME_MODE_CANTONESE) ? 1 : 0;
        g_candidates = Dictionary::Instance().GetCandidates(purePinyin, mode, purePinyin);

        // 繁体模式：候选直接显示繁体，选中后上屏的也是繁体
        if (g_chineseVariant == IME_VARIANT_TRADITIONAL)
        {
            for (DictEntry& e : g_candidates)
                e.word = e.wordTrad.empty() ? TraditionalFallback(e.word) : e.wordTrad;
        }
    }

    // 从组合串里消耗 nLetters 个字母字符（跳过标点、数字等）
    // 例如组合串="nin"hao，消耗 3 个字母 → erase("nin")，剩余 = "hao
    void ConsumeLetters(std::string& s, size_t nLetters)
    {
        size_t consumed = 0;
        size_t lettersSeen = 0;
        for (size_t i = 0; i < s.size() && lettersSeen < nLetters; ++i)
        {
            ++consumed;
            if (isalpha((unsigned char)s[i]))
                ++lettersSeen;
        }
        s.erase(0, consumed);
    }

    // 提交一个候选词
    bool CommitCandidate(int index)
    {
        if (index < 0 || index >= (int)g_candidates.size()) return false;

        const DictEntry& chosen = g_candidates[index];
        g_committed = chosen.word;

        if (chosen.consumeInputLen > 0)
        {
            // 逐级回退选中的候选（如输入 ryjx 选中「如意」ry）：
            // 消耗指定数量的纯拼音字母，自动跳过标点
            size_t n = chosen.consumeInputLen;
            if (g_pinyinMode == IME_PINYIN_DOUBLE)
            {
                // consumeInputLen 是解码后的拼音字母数，换算为原始双拼按键数
                size_t rawKeys = ShuangpinRawKeysForDecodedLen(g_composition, n);
                ConsumeShuangpinKeys(g_composition, rawKeys);
            }
            else
            {
                if (n > g_composition.size()) n = g_composition.size();
                ConsumeLetters(g_composition, n);
            }
        }
        else if (chosen.viaInitials)
        {
            // 声母简拼匹配（如 nh → 你好）：整个组合串都消耗（含标点，全部清空）
            g_composition.clear();
        }
        else
        {
            // 连续选词：截掉候选拼音与组合串的纯公共前缀字母数
            //  - 输入 "dou" 时选「豆」(dou) → 余下 bo，刷新候选继续选词；
            //  - 只输入声母 n 时选「你」(ni) → 公共前缀 n 被消耗。
            size_t matchLen = 0;
            if (g_pinyinMode == IME_PINYIN_DOUBLE)
            {
                // 双拼：用解码后的完整拼音与候选拼音求公共前缀长度
                std::string decoded = ShuangpinDecode(g_composition);
                while (matchLen < chosen.pinyin.size() && matchLen < decoded.size())
                {
                    if (chosen.pinyin[matchLen] != decoded[matchLen]) break;
                    ++matchLen;
                }
                size_t rawKeys = ShuangpinRawKeysForDecodedLen(g_composition, matchLen);
                ConsumeShuangpinKeys(g_composition, rawKeys);
            }
            else
            {
                // 注意：组合串里可能混有标点，所以先算纯拼音公共前缀，
                // 再用 ConsumeLetters 跳回原始串消耗（把标点也一并消耗掉）。
                size_t j = 0;  // g_composition 里的字母位置
                while (matchLen < chosen.pinyin.size() && j < g_composition.size())
                {
                    if (!isalpha((unsigned char)g_composition[j])) { ++j; continue; }
                    if (chosen.pinyin[matchLen] != (char)tolower((unsigned char)g_composition[j]))
                        break;
                    ++matchLen;
                    ++j;
                }
                ConsumeLetters(g_composition, matchLen);
            }
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

static ImeWidthMode g_widthMode = IME_WIDTH_HALFWIDTH;

void IME_SetWidthMode(ImeWidthMode mode)
{
    g_widthMode = mode;
}

ImeWidthMode IME_GetWidthMode()
{
    return g_widthMode;
}

void IME_SetChineseVariant(ImeChineseVariant variant)
{
    if (g_chineseVariant == variant) return;

    g_chineseVariant = variant;

    // 正在拼写时立即按新字形刷新候选（不打断组合串）
    if (!g_composition.empty())
        RefreshCandidates();
}

ImeChineseVariant IME_GetChineseVariant()
{
    return g_chineseVariant;
}

void IME_SetPinyinMode(ImePinyinMode mode)
{
    if (g_pinyinMode != mode)
    {
        g_pinyinMode = mode;
        // 切换全拼/双拼时清空当前组合，避免候选与新方式不一致
        g_composition.clear();
        g_candidates.clear();
    }
}

ImePinyinMode IME_GetPinyinMode()
{
    return g_pinyinMode;
}

// ASCII → 全角字符：空格转 U+3000，可打印字符(0x21-0x7E) 加 0xFEE0
wchar_t IME_ConvertCharWidth(char ch)
{
    wchar_t wc = (wchar_t)(unsigned char)ch;
    if (g_widthMode == IME_WIDTH_FULLWIDTH)
    {
        if (ch == ' ')
            return 0x3000;
        if (ch >= 0x21 && ch <= 0x7E)
            return (wchar_t)((unsigned char)ch + 0xFEE0);
    }
    return wc;
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

    // 英文模式：字母、数字、空格直接提交；标点根据中文/英文标点模式转换后提交
    if (g_mode == IME_MODE_ENGLISH)
    {
        if (isalnum((unsigned char)ch) || ch == ' ')
        {
            wchar_t wc = IME_ConvertCharWidth(ch);
            CommitRaw(std::wstring(1, wc));
            return true;
        }
        // 标点符号：根据中文/英文标点模式选择输出形式；
        // 英文标点(ASCII)在全角宽度模式下再转成对应全角字符
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
            if (g_punctMode == IME_PUNCT_HALFWIDTH)
                wc = IME_ConvertCharWidth((char)wc);
            CommitRaw(std::wstring(1, wc));
            return true;
        }
        return false;
    }

    // 普通话 / 粤语模式
    const unsigned char uch = (unsigned char)ch;
    if (isalpha(uch))
    {
        // 字母：追加到组合串
        g_composition.push_back((char)tolower(uch));
        RefreshCandidates();
        return true;
    }

    if (ispunct(uch))
    {
        // 标点：拼音模式下也进组合串，让分词器统一跳过标点来识别拼音
        // （候选窗就能完整显示用户输入的 "nin"shizuncheng）
        g_composition.push_back(ch);
        RefreshCandidates();
        return true;
    }

    if (ch == ' ')
    {
        // 空格：有组合串时提交第一个候选，否则提交空格（全角模式下为全角空格）
        if (!g_composition.empty())
        {
            return CommitCandidate(0);
        }
        else
        {
            CommitRaw(std::wstring(1, IME_ConvertCharWidth(' ')));
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

    // 其它字符：有组合串时先不处理，交给前端；无组合串时直接提交（全角模式转全角）
    if (g_composition.empty())
    {
        wchar_t wc = IME_ConvertCharWidth(ch);
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

bool IME_DeleteCompositionFront(int n)
{
    if (n <= 0 || g_composition.empty()) return false;
    if ((size_t)n > g_composition.size()) n = (int)g_composition.size();
    g_composition.erase(0, n);
    RefreshCandidates();
    return true;
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

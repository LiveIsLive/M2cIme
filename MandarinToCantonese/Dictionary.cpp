#include "pch.h"
#include "Dictionary.h"
#include "PinyinSegmenter.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace
{
    // 取一个音节的声母：zh/ch/sh 取两个字母，其它取首字母
    //（零声母音节如 a/ai/an/er 取首字母，便于用 a 简拼到「爱」「安」等）
    std::string InitialOfSyllable(const std::string& syllable)
    {
        if (syllable.size() >= 2)
        {
            const std::string head = syllable.substr(0, 2);
            if (head == "zh" || head == "ch" || head == "sh") return head;
        }
        return syllable.substr(0, 1);
    }

    // 首音节是否为「鼻音自成音节」：n / ng / m / hm / hng（如「嗯」「呒」）。
    // 这类音节属于韵母性质（嗯=en/ng），没有真正的声母，
    // 输入单字母 n / m 时应排在真正声母字（你、那、吗、们…）之后。
    bool IsNasalOnlyPinyin(const std::string& pinyin)
    {
        if (pinyin.size() == 1) return pinyin == "n" || pinyin == "m";
        if (pinyin.compare(0, 3, "hng") == 0) return true;
        const std::string head = pinyin.substr(0, 2);
        return head == "ng" || head == "hm";
    }

    // 拼音串能否完整切分为合法音节（"nihao" -> ni+hao 成立；单独的 "n" 不成立）
    bool IsValidPinyinSequence(const std::string& pinyin)
    {
        std::string remainder;
        const auto syllables = PinyinSegmenter::Segment(pinyin, &remainder);
        return !syllables.empty() && remainder.empty();
    }

    // 由连续拼音生成各音节声母拼接（"nihao" -> "nh"）
    std::string BuildInitials(const std::string& pinyin)
    {
        std::string out;
        std::string remainder;
        for (const auto& s : PinyinSegmenter::Segment(pinyin, &remainder))
            out += InitialOfSyllable(s);
        return out;
    }

    // 外置词库文件名（相对于 DLL 所在目录 / 安装目录）
    const wchar_t* kDictSubdir      = L"dicts";
    const wchar_t* kMandarinDictFile = L"pinyin_simp.dict.yaml";
    const wchar_t* kCantoneseDictFile = L"joy.pinyin_yueyu.dict.yaml";

    // 获取当前 DLL 所在目录（词库相对安装目录定位）
    std::wstring GetModuleDir()
    {
        HMODULE hMod = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&GetModuleDir), &hMod) || hMod == nullptr)
            return L"";

        wchar_t path[MAX_PATH * 2] = {};
        DWORD n = GetModuleFileNameW(hMod, path, (DWORD)(sizeof(path) / sizeof(path[0])));
        if (n == 0) return L"";

        std::wstring full(path, n);
        size_t pos = full.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? L"" : full.substr(0, pos);
    }

    bool FileExists(const std::wstring& path)
    {
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    // UTF-8 -> UTF-16
    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }
}

Dictionary& Dictionary::Instance()
{
    static Dictionary inst;
    inst.EnsureLoaded();
    return inst;
}

void Dictionary::EnsureLoaded()
{
    if (loaded_) return;
    LoadExternal();
    loaded_ = true;
}

void Dictionary::LoadExternal()
{
    const std::wstring dir = GetModuleDir();
    if (dir.empty()) return;

    // 优先查找 <DLL目录>\dicts\ 子目录，其次查找 <DLL目录> 本身
    const std::wstring sub = dir + L"\\" + kDictSubdir + L"\\";
    const std::wstring flat = dir + L"\\";

    struct
    {
        const wchar_t* file;
        LangSource     source;
        bool           hasWeight;
    } specs[] =
    {
        { kMandarinDictFile,  LangSource::Mandarin,  true  },
        { kCantoneseDictFile, LangSource::Cantonese, false },
    };

    entries_.reserve(70000);

    for (const auto& s : specs)
    {
        std::wstring path = sub + s.file;
        if (!FileExists(path))
        {
            path = flat + s.file;
            if (!FileExists(path)) continue;
        }
        LoadFromFile(path, s.source, s.hasWeight);
    }
}

bool Dictionary::LoadFromFile(const std::wstring& path, LangSource source, bool hasWeight)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    size_t added = 0;
    bool headerDone = false;   // RIME 词库头部在 "..." 行结束
    std::string line;
    line.reserve(256);

    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (!headerDone)
        {
            // 跳过 YAML 头部（--- 到 ... 之间）
            if (line == "...") headerDone = true;
            continue;
        }

        if (line.empty() || line[0] == '#') continue;

        // 格式：汉字 \t 拼音(可含空格分隔音节) [\t 权重]
        const size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const size_t t2 = line.find('\t', t1 + 1);

        const std::string word8 = line.substr(0, t1);
        std::string pinyin = (t2 == std::string::npos)
            ? line.substr(t1 + 1)
            : line.substr(t1 + 1, t2 - t1 - 1);

        int weight;
        if (hasWeight && t2 != std::string::npos)
        {
            weight = atoi(line.c_str() + t2 + 1);
        }
        else
        {
            // 无权重列（sort: original）：按文件行序生成递减权重，保持原词频排序
            weight = 1000000 - (int)added;
        }

        // 去掉音节间空格并转小写，得到连续拼音（如 "ni hao" -> "nihao"）
        std::string key;
        key.reserve(pinyin.size());
        for (char c : pinyin)
        {
            if (c == ' ' || c == '\'') continue;
            key.push_back((char)tolower((unsigned char)c));
        }
        if (word8.empty() || key.empty()) continue;

        std::wstring word = Utf8ToWide(word8);
        if (word.empty()) continue;

        const std::string initials = BuildInitials(key);
        const bool nasalOnly = IsNasalOnlyPinyin(key);
        const bool validPinyin = IsValidPinyinSequence(key);
        entries_.push_back({ std::move(word), std::move(key), std::move(initials), weight, source,
                             nasalOnly, validPinyin });
        ++added;
    }

    return added > 0;
}

void Dictionary::AddEntry(const std::wstring& word, const std::string& pinyin,
                          int weight, LangSource source)
{
    entries_.push_back({ word, pinyin, BuildInitials(pinyin), weight, source,
                         IsNasalOnlyPinyin(pinyin), IsValidPinyinSequence(pinyin) });
}

std::vector<DictEntry> Dictionary::GetCandidates(const std::string& pinyin, int mode,
                                                 const std::string& initialsQuery) const
{
    std::vector<DictEntry> result;
    if (pinyin.empty()) return result;

    const size_t plen = pinyin.size();
    const size_t qlen = initialsQuery.size();

    // 匹配规则：
    //  1) 拼音匹配：词条拼音以输入串开头（输入了词的前缀），
    //     或输入串以词条拼音开头（输入了更多，词是前缀的一部分）；
    //  2) 声母匹配：输入串恰好是各音节声母拼接的前缀（如输入 nh 命中「你好」）。
    for (const auto& e : entries_)
    {
        const size_t elen = e.pinyin.size();
        bool pinyinMatch = false;
        if (elen >= plen)
        {
            // 词条拼音以输入串开头：用户可能还在输入该词
            pinyinMatch = (e.pinyin.compare(0, plen, pinyin) == 0);
        }
        else
        {
            // 输入串以词条拼音开头：词已完整，可能后面还跟着下一个词的拼音
            pinyinMatch = (pinyin.compare(0, elen, e.pinyin) == 0);
        }

        bool initialsMatch = false;
        if (!pinyinMatch && qlen > 0 && e.initials.size() >= qlen)
        {
            initialsMatch = (e.initials.compare(0, qlen, initialsQuery) == 0);
        }

        if (pinyinMatch || initialsMatch)
        {
            result.push_back(e);
            result.back().viaInitials = initialsMatch;
        }
    }

    // ===== 基于模式的优先级排序 =====
    // mode == 0 : 普通话模式 —— 粤语词条强制排到最后
    // mode == 1 : 粤语模式   —— 粤语词条强制排到最前
    //
    // 实现：给每条候选计算一个调整分 score。
    //  - 粤语模式：Cantonese 的 score = weight + kBoost；Mandarin = weight
    //  - 普通话模式：Mandarin 的 score = weight + kBoost；Cantonese = weight - kBoost
    // 这样同一来源内部仍按词频排序，不同来源之间严格隔离。
    // 注意：kBoost 必须大于任何词条权重（外置普通话词库权重可达数百万）。
    const long long kBoost = 1000000000000LL; // 1e12

    auto scoreOf = [&](const DictEntry& e) -> long long
    {
        const long long w = e.weight;
        if (mode == 1) // 粤语模式
        {
            return (e.source == LangSource::Cantonese) ? (w + kBoost) : w;
        }
        else // 普通话模式
        {
            return (e.source == LangSource::Mandarin) ? (w + kBoost) : (w - kBoost);
        }
    };

    std::stable_sort(result.begin(), result.end(),
        [&](const DictEntry& a, const DictEntry& b)
        {
            // 匹配优先级（高→低）：
            //  0 拼音完全匹配 + 可完整切分      （如输入 ni → 你；nihao → 你好）
            //  1 声母完全匹配                   （如输入 nh → 你好）
            //  2 拼音前缀匹配 + 可完整切分 + 单字（如输入 n → 你/那/年…）
            //  3 拼音前缀匹配 + 可完整切分 + 多字词（如输入 n → 那个/你们…）
            //  4 声母前缀匹配
            //  5 拼音匹配但切分不完整 / 鼻音自成音节（如输入 n → 嗯「拼音 n/ng 不是真正声母」）
            //
            // 关键：
            //  1) 输入单个字母时，先列该声母的单字，再列词组，最后才是「嗯」这类非真正声母；
            //  2) 嗯(pinyin=n) 这种残拼音完全匹配不会压过「你」(ni)；
            //  3) 嗯(ng)、呒(m) 这类鼻音自成音节排在所有 n/m 声母字之后；
            //  4) 多字词（你好=nihao）能完整切分，仍按完全匹配拿最高优先级；
            //  5) 输入完整音节（如 ng、en）时仍按完全匹配优先。
            auto rankOf = [&](const DictEntry& e) -> int
            {
                if (!e.viaInitials)
                {
                    const bool singleChar = (e.word.size() == 1);
                    if (e.pinyin.size() == plen)
                        return e.validPinyin ? 0 : 5;
                    if (e.nasalOnly || !e.validPinyin) return 5;
                    return singleChar ? 2 : 3;
                }
                return (e.initials.size() == qlen) ? 1 : 4;
            };
            const int ra = rankOf(a);
            const int rb = rankOf(b);
            if (ra != rb) return ra < rb;

            long long sa = scoreOf(a);
            long long sb = scoreOf(b);
            if (sa != sb) return sa > sb;
            // 同分下，拼音更短（更精确匹配）的优先
            if (a.pinyin.size() != b.pinyin.size())
                return a.pinyin.size() < b.pinyin.size();
            return a.weight > b.weight;
        });

    return result;
}

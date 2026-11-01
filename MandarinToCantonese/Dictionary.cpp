#include "pch.h"
#include "Dictionary.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace
{
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

        entries_.push_back({ std::move(word), std::move(key), weight, source });
        ++added;
    }

    return added > 0;
}

void Dictionary::AddEntry(const std::wstring& word, const std::string& pinyin,
                          int weight, LangSource source)
{
    entries_.push_back({ word, pinyin, weight, source });
}

std::vector<DictEntry> Dictionary::GetCandidates(const std::string& pinyin, int mode) const
{
    std::vector<DictEntry> result;
    if (pinyin.empty()) return result;

    const size_t plen = pinyin.size();

    // 匹配规则：词条拼音以输入串开头（输入了词的前缀）
    // 或输入串以词条拼音开头（输入了更多，词是前缀的一部分）
    for (const auto& e : entries_)
    {
        const size_t elen = e.pinyin.size();
        bool match = false;
        if (elen >= plen)
        {
            // 词条拼音以输入串开头：用户可能还在输入该词
            match = (e.pinyin.compare(0, plen, pinyin) == 0);
        }
        else
        {
            // 输入串以词条拼音开头：词已完整，可能后面还跟着下一个词的拼音
            match = (pinyin.compare(0, elen, e.pinyin) == 0);
        }
        if (match) result.push_back(e);
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
            // 全匹配（词条拼音与输入串完全一致）的候选优先于一切前缀匹配，
            // 例如输入 "nihao" 时「你好」排在「你」之前。
            const bool aExact = (a.pinyin.size() == plen);
            const bool bExact = (b.pinyin.size() == plen);
            if (aExact != bExact) return aExact;

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

#include "pch.h"
#include "Dictionary.h"
#include "PinyinSegmenter.h"
#include "third_party/sqlite/sqlite3.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

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

    // 按空格 / 隔音符号拆分音节；词库没写分隔符时退回自动切分（"nihao" -> ni, hao）
    std::vector<std::string> SplitSyllables(const std::string& raw, const std::string& key)
    {
        std::vector<std::string> out;
        std::string cur;
        cur.reserve(8);
        for (char c : raw)
        {
            if (c == ' ' || c == '\'')
            {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            }
            else
            {
                cur.push_back((char)tolower((unsigned char)c));
            }
        }
        if (!cur.empty()) out.push_back(cur);

        if (out.size() <= 1)
        {
            std::string remainder;
            const auto seg = PinyinSegmenter::Segment(key, &remainder);
            if (!seg.empty() && remainder.empty()) out = seg;
        }
        return out;
    }

    // 把各音节长度编码成一串字节（拼音音节最长 6 字母），省去为每条词存音节字符串
    std::string EncodeSyllableLengths(const std::vector<std::string>& syllables)
    {
        std::string out;
        out.reserve(syllables.size());
        for (const auto& s : syllables) out.push_back((char)s.size());
        return out;
    }

    // 由音节列表生成声母拼接（"ni","hao" -> "nh"）
    std::string BuildInitials(const std::vector<std::string>& syllables)
    {
        std::string out;
        for (const auto& s : syllables) out += InitialOfSyllable(s);
        return out;
    }

    // 输入串已打到第几个音节：音节起始位置落在输入串内的都算已进入
    //（如 豆瓣 dou|ban 输入 douba 时覆盖 2 个音节，输入 dou 时只覆盖 1 个）
    size_t CoveredSyllableCount(const std::string& sylLens, size_t queryLen)
    {
        size_t covered = 0;
        size_t off = 0;
        for (size_t i = 0; i < sylLens.size(); ++i)
        {
            if (off >= queryLen) break;
            ++covered;
            off += (size_t)(unsigned char)sylLens[i];
        }
        return covered;
    }

    // 输入串已经开始的音节数，即声母个数，用于限制候选词的字数：
    //   "suoyou" -> suo + you        -> 2（s + y）
    //   "suoy"   -> suo + y(未输完)  -> 2
    //   "nihaom" -> ni + hao + m(未输完) -> 3
    //   "jx"     -> 纯声母串         -> 2（每个声母字母算一个）
    size_t CountInputInitials(const std::string& query)
    {
        // 声母字母（含 zh/ch/sh 的 z/c/s、零声母音节的 y/w）
        static const char kInitialLetters[] = "bpmfdtnlgkhjqxzcsryw";

        std::string remainder;
        const auto syllables = PinyinSegmenter::Segment(query, &remainder);

        size_t count = syllables.size();
        // 尾部尚未成型的音节：每个声母字母代表又开了一个音节（如 nhao 的 n、h）
        for (char c : remainder)
        {
            if (std::string(kInitialLetters).find(c) != std::string::npos)
                ++count;
        }
        return (count > 0) ? count : query.size();
    }

    // 第 off 个字符起、长度为 len 的音节的声母长度（zh/ch/sh 为 2，其余为 1）
    size_t InitialLenAt(const std::string& pinyin, size_t off, size_t len)
    {
        if (len >= 2)
        {
            const char c0 = pinyin[off];
            const char c1 = pinyin[off + 1];
            if ((c0 == 'z' || c0 == 'c' || c0 == 's') && c1 == 'h') return 2;
        }
        return 1;
    }

    // 声母简拼匹配结果：
    //   None   —— 不匹配
    //   Prefix —— 已匹配若干音节，输入串用完但词还有剩余音节（如 sm -> 说明书）
    //   Full   —— 输入串正好覆盖全部音节（如 sms / shmsh -> 说明书）
    enum class InitialsMatch { None, Prefix, Full };

    // 逐音节匹配声母简拼。每个音节的声母既可写完整形式（zh/ch/sh 两个字母），
    // 也可只写首字母（z/c/s 分别代表 zh/ch/sh），
    // 因此「说明书」(shuo ming shu) 用 shmsh 或 sms 都能命中。
    InitialsMatch MatchInitials(const std::string& pinyin, const std::string& sylLens,
                                const std::string& query)
    {
        if (query.empty() || sylLens.empty()) return InitialsMatch::None;

        size_t qpos = 0;
        size_t off = 0;
        for (size_t i = 0; i < sylLens.size(); ++i)
        {
            const size_t len = (size_t)(unsigned char)sylLens[i];
            if (len == 0 || off + len > pinyin.size()) return InitialsMatch::None;

            if (qpos == query.size())
                return (i > 0) ? InitialsMatch::Prefix : InitialsMatch::None;

            const size_t initialLen = InitialLenAt(pinyin, off, len);
            // 1) 完整声母（zh / ch / sh 两个字母）
            if (initialLen > 1 && qpos + initialLen <= query.size() &&
                query.compare(qpos, initialLen, pinyin, off, initialLen) == 0)
            {
                qpos += initialLen;
            }
            // 2) 首字母声母（z / c / s 也可代表 zh / ch / sh）
            else if (query[qpos] == pinyin[off])
            {
                ++qpos;
            }
            else
            {
                return InitialsMatch::None;
            }
            off += len;
        }
        return (qpos == query.size()) ? InitialsMatch::Full : InitialsMatch::None;
    }

    // 混拼匹配结果：
    //   None    —— 不匹配
    //   Partial —— 已完整匹配部分音节，尾部仍在输入（如 ni + h -> 你好）
    //   Full    —— 所有音节都匹配且输入串刚好用完（如 n + hao 或 ni + h -> 你好）
    enum class MixedMatch { None, Partial, Full };

    // 逐音节匹配组合串：每个音节既可以全拼匹配，也可以用声母匹配（混拼），
    // 尾部允许只输入了音节的一部分。idx 为音节序号，off 为音节在 pinyin 中的偏移。
    MixedMatch MatchMixedRec(const std::string& pinyin, const std::string& sylLens,
                             size_t idx, size_t off,
                             const std::string& query, size_t qpos)
    {
        if (qpos == query.size())
        {
            if (idx == sylLens.size()) return MixedMatch::Full;
            return (idx > 0) ? MixedMatch::Partial : MixedMatch::None;
        }
        if (idx == sylLens.size()) return MixedMatch::None;

        const size_t len = (size_t)(unsigned char)sylLens[idx];
        const size_t initialLen = InitialLenAt(pinyin, off, len);

        // 1) 该音节全拼（如 nihao 的 hao）
        if (qpos + len <= query.size() && query.compare(qpos, len, pinyin, off, len) == 0)
        {
            const MixedMatch r = MatchMixedRec(pinyin, sylLens, idx + 1, off + len, query, qpos + len);
            if (r != MixedMatch::None) return r;
        }
        // 2) 该音节声母（如 nihao 的 n / h）
        if (qpos + initialLen <= query.size() &&
            query.compare(qpos, initialLen, pinyin, off, initialLen) == 0)
        {
            const MixedMatch r = MatchMixedRec(pinyin, sylLens, idx + 1, off + len, query, qpos + initialLen);
            if (r != MixedMatch::None) return r;
        }
        // 2b) zh/ch/sh 只输入首字母（如 sms 的最后一个 s 代表「书」的 sh）
        if (initialLen > 1 && qpos < query.size() && query[qpos] == pinyin[off])
        {
            const MixedMatch r = MatchMixedRec(pinyin, sylLens, idx + 1, off + len, query, qpos + 1);
            if (r != MixedMatch::None) return r;
        }
        // 3) 该音节只输入了一部分（正在输入中，如 nha 里的 ha）
        const size_t rest = query.size() - qpos;
        if (idx > 0 && rest < len && pinyin.compare(off, rest, query, qpos, rest) == 0)
            return MixedMatch::Partial;

        return MixedMatch::None;
    }

    // 外置词库文件名（相对于 DLL 所在目录 / 安装目录）
    const wchar_t* kDictSubdir = L"dicts";
    const wchar_t* kDictFile   = L"m2c_dict.db";

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

    // UTF-16 -> UTF-8（SQLite 需要 UTF-8 路径）
    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
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

    // 优先查找 <DLL目录>\dicts\m2c_dict.db，其次查找 <DLL目录> 本身
    std::wstring path = dir + L"\\" + kDictSubdir + L"\\" + kDictFile;
    if (!FileExists(path))
    {
        path = dir + L"\\" + kDictFile;
        if (!FileExists(path)) return;
    }
    LoadFromDatabase(path);
}

bool Dictionary::LoadFromDatabase(const std::wstring& path)
{
    sqlite3* db = nullptr;
    const std::string dbPath = WideToUtf8(path);
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return false;
    }

    // id 即导入顺序（普通话在前、粤语在后），
    // 按它读入可保持与原文本词库一致的同权重排序。
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT word, pinyin, initials, syl_lens, weight, source, nasal_only, valid_pinyin"
        " FROM entries ORDER BY id";

    size_t added = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        entries_.reserve(70000);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char* word8 = sqlite3_column_text(stmt, 0);
            const unsigned char* pinyin8 = sqlite3_column_text(stmt, 1);
            const unsigned char* initials8 = sqlite3_column_text(stmt, 2);
            if (!word8 || !pinyin8 || !initials8) continue;

            std::wstring word = Utf8ToWide(reinterpret_cast<const char*>(word8));
            if (word.empty()) continue;

            const void* blob = sqlite3_column_blob(stmt, 3);
            const int blobLen = sqlite3_column_bytes(stmt, 3);
            std::string sylLens;
            if (blob && blobLen > 0)
                sylLens.assign(static_cast<const char*>(blob), (size_t)blobLen);

            entries_.push_back({
                std::move(word),
                reinterpret_cast<const char*>(pinyin8),
                reinterpret_cast<const char*>(initials8),
                std::move(sylLens),
                sqlite3_column_int(stmt, 4),
                (sqlite3_column_int(stmt, 5) == 1) ? LangSource::Cantonese : LangSource::Mandarin,
                sqlite3_column_int(stmt, 6) != 0,
                sqlite3_column_int(stmt, 7) != 0
            });
            ++added;
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return added > 0;
}

void Dictionary::AddEntry(const std::wstring& word, const std::string& pinyin,
                          int weight, LangSource source)
{
    const auto syllables = SplitSyllables(pinyin, pinyin);
    entries_.push_back({ word, pinyin, BuildInitials(syllables), EncodeSyllableLengths(syllables),
                         weight, source, IsNasalOnlyPinyin(pinyin), IsValidPinyinSequence(pinyin) });
}

std::vector<DictEntry> Dictionary::QueryOnce(const std::string& pinyin, int mode,
                                             const std::string& initialsQuery) const
{
    std::vector<DictEntry> result;
    if (pinyin.empty()) return result;

    const size_t plen = pinyin.size();
    const size_t qlen = initialsQuery.size();

    // 匹配规则：
    //  1) 拼音匹配：词条拼音以输入串开头（输入了词的前缀），
    //     或输入串以词条拼音开头（输入了更多，词是前缀的一部分）；
    //  2) 声母匹配：输入串恰好是各音节声母拼接的前缀（如输入 nh 命中「你好」）；
    //  3) 混拼匹配：逐音节匹配，每个音节可以用全拼或声母
    //     （如 nhao = n + hao、nih = ni + h，都能命中「你好」）。
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

        // 鼻音自成音节词条（嗯/唔…）拼音很短（n/ng/m），输入串比它长时
        // 只是靠「词条拼音正好是输入串前缀」混进来，属于噪声，直接跳过。
        // 输入 n / ng 这类等长或更短的输入不受影响（它们仍排在所有 n 声母字之后）。
        if (pinyinMatch && elen < plen && e.nasalOnly)
            continue;

        // 声母简拼：输入串恰好是各音节声母拼接的前缀（如输入 nh 命中「你好」、
        // sms 命中「说明书」）。zh/ch/sh 的声母可写两个字母，也可只写首字母。
        bool initialsFull = false;
        bool initialsPrefix = false;
        if (!pinyinMatch && qlen > 0)
        {
            if (!e.sylLens.empty())
            {
                const InitialsMatch im = MatchInitials(e.pinyin, e.sylLens, initialsQuery);
                initialsFull = (im == InitialsMatch::Full);
                initialsPrefix = (im == InitialsMatch::Prefix);
            }
            else if (e.initials.size() >= qlen &&
                     e.initials.compare(0, qlen, initialsQuery) == 0)
            {
                // 无音节长度信息时退回整串声母前缀比较
                initialsFull = (e.initials.size() == qlen);
                initialsPrefix = !initialsFull;
            }
        }
        const bool initialsMatch = initialsFull || initialsPrefix;

        // 混拼：至少两个音节时才有意义（单音节用全拼 / 声母前缀已能覆盖），
        // 首字母必须相同，避免对全词库做无意义的逐音节匹配。
        bool mixedFull = false;
        bool mixedPartial = false;
        if (plen >= 2 && e.sylLens.size() >= 2 && e.pinyin[0] == pinyin[0])
        {
            const MixedMatch mm = MatchMixedRec(e.pinyin, e.sylLens, 0, 0, initialsQuery, 0);
            mixedFull = (mm == MixedMatch::Full);
            mixedPartial = (mm == MixedMatch::Partial);
        }

        if (!pinyinMatch && !initialsMatch && !mixedFull && !mixedPartial)
            continue;

        // 匹配优先级（越小越靠前）：
        //  0 拼音完全匹配 + 可完整切分      （如输入 ni → 你；nihao → 你好）
        //  1 混拼 / 声母完整匹配，或已输入到第 2 个音节及以后的多字词
        //                                    （如 nih、nhao、nh → 你好；douba → 豆瓣；wom → 我们）
        //  2 拼音前缀匹配 + 可完整切分 + 单字（如输入 n → 你/那/年…；ni → 你）
        //  3 拼音前缀匹配 + 可完整切分 + 多字词（如输入 n → 那个/你们…；dou → 豆瓣）
        //  4 声母前缀匹配
        //  5 拼音匹配但切分不完整 / 鼻音自成音节（如输入 n → 嗯「拼音 n/ng 不是真正声母」）
        int rank = 0;
        if (pinyinMatch)
        {
            if (elen == plen) rank = e.validPinyin ? 0 : 5;
            else if (e.nasalOnly || !e.validPinyin) rank = 5;
            else if (mixedFull) rank = 1;            // nih / nhao → 你好
            else if (CoveredSyllableCount(e.sylLens, plen) >= 2) rank = 1; // douba → 豆瓣
            else rank = (e.word.size() == 1) ? 2 : 3;
        }
        else if (initialsFull || mixedFull)
        {
            rank = 1;   // 声母 / 混拼完整匹配（nh → 你好；sms → 说明书）
        }
        else if (initialsPrefix)
        {
            rank = 4;   // 声母前缀匹配（只命中词的前几个音节）
        }
        else
        {
            rank = 3;   // 混拼前缀（还在输入中，如 nha → 你好）
        }

        // 本次实际覆盖到的词条拼音字符数：
        //  - pinyinMatch 时 = min(plen, elen)
        //  - mixedFull    时 = plen   （输入串刚好覆盖所有音节，算完整）
        //  - mixedPartial 时 = plen   （尾部仍在输入中，已覆盖全部已匹配音节）
        //  - initialsMatch 时 = 0    （声母简拼不直接对应拼音字符长度，不用于"更长完整匹配"）
        size_t matchedLen = 0;
        if (mixedFull)         matchedLen = plen;
        else if (pinyinMatch)  matchedLen = (std::min)(plen, elen);

        result.push_back(e);
        DictEntry& added = result.back();
        added.viaInitials = (initialsMatch || mixedFull || mixedPartial);
        added.matchRank = rank;
        added.matchedLen = matchedLen;
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
            // 匹配优先级由 matchRank 给出（见上方匹配循环），这里只负责按它排序。
            // 关键：
            //  1) 输入单个字母时，先列该声母的单字，再列词组，最后才是「嗯」这类非真正声母；
            //  2) 嗯(pinyin=n) 这种残拼音完全匹配不会压过「你」(ni)；
            //  3) 嗯(ng)、呒(m) 这类鼻音自成音节排在所有 n/m 声母字之后；
            //  4) 多字词（你好=nihao）能完整切分，仍按完全匹配拿最高优先级；
            //  5) 输入完整音节（如 ng、en）时仍按完全匹配优先；
            //  6) 混拼完整匹配（nih / nhao / nh → 你好）排在单字前缀之前；
            //  7) 已打到第 2 个音节及以后时，多字词排在单字之前（douba → 豆瓣，wom → 我们）。
            if (a.matchRank != b.matchRank) return a.matchRank < b.matchRank;

            // 同优先级下，"更长完整匹配"优先：
            //  - 输入 nihaom 时「你好吗」matchedLen=5 > 「你好」matchedLen=4 → 你好吗排前
            //  - 输入 nihao  时「你好吗」matchedLen=4 = 「你好」matchedLen=4 → 走后续词频比较
            if (a.matchedLen != b.matchedLen) return a.matchedLen > b.matchedLen;

            long long sa = scoreOf(a);
            long long sb = scoreOf(b);
            if (sa != sb) return sa > sb;
            // 同分同匹配长度下，拼音更短（更精确）的优先
            if (a.pinyin.size() != b.pinyin.size())
                return a.pinyin.size() < b.pinyin.size();
            return a.weight > b.weight;
        });

    // 候选词字数不得超过输入串已声明的声母个数（音节数）：
    // 例如输入 suoyou（s + y 两个声母）时只保留 2 字以内的词，
    // 不再列出「所有人」「所有作品」这类靠前缀补全混进来的更长词；
    // 纯声母输入（jx、ryjx）同样按声母个数（字母数）限制，排除「教学活动」「集线器」。
    // 仅裁掉拼音比输入更长的“补全”词：拼音与输入等长的属于精确匹配，
    // 不受 xian（先 / 西安）这类切分歧义影响。
    const size_t inputInitials = CountInputInitials(initialsQuery.empty() ? pinyin : initialsQuery);
    if (inputInitials >= 2)
    {
        result.erase(std::remove_if(result.begin(), result.end(),
            [inputInitials, plen](const DictEntry& e)
            {
                return e.pinyin.size() > plen && e.word.size() > inputInitials;
            }),
            result.end());
    }

    return result;
}

std::vector<DictEntry> Dictionary::GetCandidates(const std::string& pinyin, int mode,
                                                 const std::string& initialsQuery) const
{
    std::vector<DictEntry> result = QueryOnce(pinyin, mode, initialsQuery);
    if (initialsQuery.size() < 2) return result;

    // 整串匹配不到任何词组时逐级回退（如输入 ryjx 想打「如意吉祥」但词库未收录）：
    // 依次列出 ryj 命中的词组、ry 命中的词组，最后是 r 声母对应的单字，
    // 让用户仍能逐段把整词选出来。
    const bool hasWordGroup = std::any_of(result.begin(), result.end(),
        [](const DictEntry& e) { return e.word.size() >= 2; });

    if (hasWordGroup)
    {
        // 纯声母输入（如 jx）命中词组时，末尾补上首字母声母对应的单字（j 开头的字），
        // 让用户既能选词也能选字。
        const bool pureInitials = !IsValidPinyinSequence(initialsQuery);
        if (pureInitials)
        {
            std::set<std::pair<std::wstring, int>> seen;
            for (const auto& e : result)
                seen.emplace(e.word, (int)e.source);
            const std::string first = initialsQuery.substr(0, 1);
            for (DictEntry& e : QueryOnce(first, mode, first))
            {
                if (e.word.size() != 1) continue;
                if (!seen.emplace(e.word, (int)e.source).second) continue;
                result.push_back(std::move(e));
            }
        }
        return result;
    }

    result.clear();
    std::set<std::pair<std::wstring, int>> seen;  // 长前缀层已列出的词不再在短前缀层重复

    auto appendLayer = [&](size_t prefixLen, bool wordGroup)
    {
        const std::string prefix = initialsQuery.substr(0, prefixLen);
        for (DictEntry& e : QueryOnce(prefix, mode, prefix))
        {
            // 词组层只留多字词，单字层只留单字
            if ((e.word.size() >= 2) != wordGroup) continue;
            if (!seen.emplace(e.word, (int)e.source).second) continue;
            // 简拼 / 混拼命中的词组只消耗匹配上的那段输入（如 ryjx 选「如意」只消耗 ry，
            // 剩余 jx 留在组合串里继续选「吉祥」）；拼音命中的沿用公共前缀消耗，无需标记。
            if (e.viaInitials)
                e.consumeInputLen = prefixLen;
            result.push_back(std::move(e));
        }
    };

    // 词组层：前 n-1 字母、前 n-2 字母……直到前 2 字母
    for (size_t len = initialsQuery.size() - 1; len >= 2; --len)
        appendLayer(len, true);
    // 单字层：首字母声母对应的单字
    appendLayer(1, false);

    return result;
}

#include "pch.h"
#include "Dictionary.h"
#include "PinyinSegmenter.h"
#include "third_party/sqlite/sqlite3.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

    // 输入串已经开始的音节数，即声母个数
    size_t CountInputInitials(const std::string& query)
    {
        static const char kInitialLetters[] = "bpmfdtnlgkhjqxzcsryw";
        std::string remainder;
        const auto syllables = PinyinSegmenter::Segment(query, &remainder);
        size_t count = syllables.size();
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

    bool IsInitialLetter(char c)
    {
        static const char kInitialLetters[] = "bpmfdtnlgkhjqxzcsryw";
        return std::strchr(kInitialLetters, c) != nullptr;
    }

    // 按「候选词生成规则」把输入切分为「独立声母 / 完整拼音音节」，尝试与词条逐音节对应
    size_t RuleConsume(const DictEntry& e, const std::string& input)
    {
        if (e.sylLens.empty() || input.empty()) return 0;
        constexpr size_t kMaxInput = 127;
        if (input.size() > kMaxInput) return 0;

        char cur[kMaxInput + 1] = {};
        char nxt[kMaxInput + 1] = {};
        cur[0] = 1;

        size_t off = 0;
        for (size_t i = 0; i < e.sylLens.size(); ++i)
        {
            const size_t len = (size_t)(unsigned char)e.sylLens[i];
            if (len == 0 || off + len > e.pinyin.size()) return 0;

            std::memset(nxt, 0, input.size() + 1);
            bool any = false;

            for (size_t pos = 0; pos < input.size(); ++pos)
            {
                if (!cur[pos]) continue;

                // 1) 完整拼音音节
                if (pos + len <= input.size() &&
                    input.compare(pos, len, e.pinyin, off, len) == 0)
                {
                    nxt[pos + len] = 1;
                    any = true;
                }

                // 2) 单字母声母
                const char c = input[pos];
                if (c == e.pinyin[off] &&
                    (IsInitialLetter(c) || !IsInitialLetter(e.pinyin[off])))
                {
                    nxt[pos + 1] = 1;
                    any = true;
                }

                // 3) 完整声母 zh/ch/sh
                if (pos + 1 < input.size())
                {
                    const char c0 = input[pos];
                    const char c1 = input[pos + 1];
                    const bool atEnd = (pos + 2 == input.size());
                    const bool nextIsInitial = atEnd || IsInitialLetter(input[pos + 2]);
                    if (nextIsInitial &&
                        (c0 == 'z' || c0 == 'c' || c0 == 's') && c1 == 'h' &&
                        off + 1 < e.pinyin.size() &&
                        e.pinyin[off] == c0 && e.pinyin[off + 1] == 'h')
                    {
                        nxt[pos + 2] = 1;
                        any = true;
                    }
                }
            }

            if (!any) return 0;
            std::memcpy(cur, nxt, input.size() + 1);
            off += len;
        }

        for (size_t p = input.size(); p >= 1; --p)
            if (cur[p]) return p;
        return 0;
    }

    // 当前输入模式下各来源的优先层级：1 = 优先，0 = 靠后。
    // 普通话模式优先普通话词库，粤语模式优先粤语词库。
    int SourceTier(const DictEntry& e, int mode)
    {
        if (mode == 1)
            return (e.source == LangSource::Cantonese) ? 1 : 0;
        return (e.source == LangSource::Mandarin) ? 1 : 0;
    }

    enum class InitialsMatch { None, Prefix, Full };

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
            if (initialLen > 1 && qpos + initialLen <= query.size() &&
                query.compare(qpos, initialLen, pinyin, off, initialLen) == 0)
            {
                qpos += initialLen;
            }
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

    enum class MixedMatch { None, Partial, Full };

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

        if (qpos + len <= query.size() && query.compare(qpos, len, pinyin, off, len) == 0)
        {
            const MixedMatch r = MatchMixedRec(pinyin, sylLens, idx + 1, off + len, query, qpos + len);
            if (r != MixedMatch::None) return r;
        }
        if (qpos + initialLen <= query.size() &&
            query.compare(qpos, initialLen, pinyin, off, initialLen) == 0)
        {
            const MixedMatch r = MatchMixedRec(pinyin, sylLens, idx + 1, off + len, query, qpos + initialLen);
            if (r != MixedMatch::None) return r;
        }
        if (initialLen > 1 && qpos < query.size() && query[qpos] == pinyin[off])
        {
            const MixedMatch r = MatchMixedRec(pinyin, sylLens, idx + 1, off + len, query, qpos + 1);
            if (r != MixedMatch::None) return r;
        }
        const size_t rest = query.size() - qpos;
        if (idx > 0 && rest < len && pinyin.compare(off, rest, query, qpos, rest) == 0)
            return MixedMatch::Partial;

        return MixedMatch::None;
    }

    const wchar_t* kDictSubdir   = L"dicts";
    const wchar_t* kDictFile     = L"m2c_dict.db";
    const wchar_t* kBackupFile   = L"m2c_dict.bakup.db";
    const char*    kBackupSchema = "m2c_bak";
    const wchar_t* kUserCustomLibName = L"用户自定义词库";

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

    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }

    std::wstring Trim(const std::wstring& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && iswspace(s[a])) ++a;
        while (b > a && iswspace(s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    bool ExecSql(sqlite3* db, const char* sql)
    {
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        return rc == SQLITE_OK;
    }

    bool ColumnExists(sqlite3* db, const char* table, const char* column)
    {
        std::string sql = "PRAGMA table_info(" + std::string(table) + ")";
        sqlite3_stmt* stmt = nullptr;
        bool found = false;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                const unsigned char* name = sqlite3_column_text(stmt, 1);
                if (name && std::strcmp(reinterpret_cast<const char*>(name), column) == 0)
                {
                    found = true;
                    break;
                }
            }
            sqlite3_finalize(stmt);
        }
        return found;
    }

    bool TableExists(sqlite3* db, const char* table)
    {
        sqlite3_stmt* stmt = nullptr;
        bool found = false;
        if (sqlite3_prepare_v2(db,
            "SELECT name FROM sqlite_master WHERE type='table' AND name=?1", -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, table, -1, SQLITE_TRANSIENT);
            found = (sqlite3_step(stmt) == SQLITE_ROW);
            sqlite3_finalize(stmt);
        }
        return found;
    }

    // 列出某个 schema（main 或 ATTACH 进来的备份库）里一张表的列名，保持表定义顺序
    std::vector<std::string> TableColumns(sqlite3* db, const char* schema, const char* table)
    {
        std::vector<std::string> cols;
        const std::string sql = std::string("PRAGMA ") + schema + ".table_info(" + table + ")";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return cols;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            if (name) cols.emplace_back(reinterpret_cast<const char*>(name));
        }
        sqlite3_finalize(stmt);
        return cols;
    }

    std::string QuoteIdentifier(const std::string& name)
    {
        std::string out = "\"";
        for (char c : name)
        {
            if (c == '"') out += '"';
            out += c;
        }
        out += '"';
        return out;
    }

    // 生成 INSERT INTO <table>(共同列) SELECT <共同列> FROM backup.<table>
    // 只复制两边都存在的列，缺失的列使用目标表默认值（例如 word_trad / library_id）
    std::string MakeCopySql(const std::vector<std::string>& dstCols,
                            const std::vector<std::string>& srcCols,
                            const char* table)
    {
        std::string cols;
        for (const auto& col : dstCols)
        {
            if ((std::find(srcCols.begin(), srcCols.end(), col) == srcCols.end())) continue;
            if (!cols.empty()) cols += ", ";
            cols += QuoteIdentifier(col);
        }
        if (cols.empty()) return std::string();
        return std::string("INSERT INTO ") + table + " (" + cols + ") SELECT " + cols +
               " FROM " + kBackupSchema + "." + table;
    }

    // ---------- QCEL 解析 ----------
    uint32_t ReadU32(const std::vector<uint8_t>& b, size_t off)
    {
        if (off + 4 > b.size()) return 0;
        return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
               ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
    }
    uint16_t ReadU16(const std::vector<uint8_t>& b, size_t off)
    {
        if (off + 2 > b.size()) return 0;
        return (uint16_t)b[off] | ((uint16_t)b[off + 1] << 8);
    }
    std::wstring ReadUtf16Z(const std::vector<uint8_t>& b, size_t off, size_t maxBytes)
    {
        std::wstring out;
        size_t end = (std::min)(b.size(), off + maxBytes);
        for (size_t i = off; i + 1 < end; i += 2)
        {
            wchar_t ch = (wchar_t)(b[i] | (b[i + 1] << 8));
            if (ch == 0) break;
            out.push_back(ch);
        }
        return out;
    }

    bool ParseQcel(const std::wstring& path, std::wstring& outName,
                   std::vector<std::pair<std::string, std::wstring>>& outPairs,
                   std::wstring& error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = L"无法打开词库文件";
            return false;
        }
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        // QQ 拼音 6.0+ 的 .qcel 与搜狗 .scel 基本相同：
        //   0x0000  4 字节魔数 0x00001540（字节序列 40 15 00 00）
        //   0x0120  不展开重码的词条数
        //   0x0124  展开重码后的词数
        //   0x0130  词库名（UTF-16LE，NUL 结尾）
        //   0x1540  拼音表：u16 数量 + 2 字节保留，随后每条 (u16 索引, u16 字节长度, UTF-16LE 拼音)
        //   拼音表之后是词表：u16 重码数 + u16 拼音索引数组字节长度 + 索引数组，
        //   然后每个词：u16 词字节长度 + UTF-16LE 词 + u16 扩展信息字节长度 + 扩展信息
        if (buf.size() < 0x1544 || ReadU32(buf, 0) != 0x00001540)
        {
            error = L"不是有效的QQ拼音词库(.qcel)文件";
            return false;
        }

        outName = Trim(ReadUtf16Z(buf, 0x130, 0x208));
        if (outName.empty())
        {
            size_t slash = path.find_last_of(L"\\/");
            std::wstring file = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
            size_t dot = file.find_last_of(L'.');
            outName = (dot == std::wstring::npos) ? file : file.substr(0, dot);
        }
        if (outName.empty()) outName = L"导入的QQ拼音词库";

        // ===== 拼音表（固定从 0x1540 开始）=====
        size_t pos = 0x1540;
        const uint32_t pinyinCount = ReadU32(buf, pos); // SCEL/qcel 为 32 位数量
        pos += 4; // 跳过数量与 2 字节保留

        std::vector<std::string> pinyins(pinyinCount);
        for (uint32_t i = 0; i < pinyinCount; ++i)
        {
            if (pos + 4 > buf.size()) break;
            const uint16_t idx = ReadU16(buf, pos); pos += 2;
            const uint16_t len = ReadU16(buf, pos); pos += 2;
            if (pos + len > buf.size()) { pos = buf.size(); break; }

            std::wstring syllable = ReadUtf16Z(buf, pos, len);
            pos += len;

            std::string py;
            py.reserve(syllable.size());
            for (wchar_t wc : syllable)
            {
                if (wc >= L'A' && wc <= L'Z') wc = (wchar_t)(wc - L'A' + L'a');
                if (wc >= 0x20 && wc < 0x7F) py.push_back((char)wc);
            }
            if (idx < pinyins.size()) pinyins[idx] = py;
        }

        // ===== 词表（紧接拼音表之后）=====
        size_t wordPos = pos;
        uint32_t blockCount = ReadU32(buf, 0x120);
        if (blockCount == 0) blockCount = ReadU32(buf, 0x124);
        // 防止损坏文件给出离谱的块数
        if (blockCount > buf.size() / 6 + 1)
            blockCount = (uint32_t)(buf.size() / 6 + 1);

        for (uint32_t block = 0; block < blockCount; ++block)
        {
            if (wordPos + 4 > buf.size()) break;
            const uint16_t repeat = ReadU16(buf, wordPos); wordPos += 2;
            const uint16_t pyBytes = ReadU16(buf, wordPos); wordPos += 2;
            if (wordPos + pyBytes > buf.size()) break;

            std::string py;
            for (uint16_t k = 0; k + 1 < pyBytes; k += 2)
            {
                const uint16_t idx = ReadU16(buf, wordPos + k);
                if (idx < pinyins.size())
                {
                    py += pinyins[idx];
                }
                else if (idx < pinyins.size() + 26)
                {
                    // 部分带英文的词库把 a-z 依次放在拼音表末尾之后
                    py.push_back((char)('a' + (idx - pinyins.size())));
                }
            }
            wordPos += pyBytes;

            std::string key;
            key.reserve(py.size());
            for (char c : py)
            {
                if (c == ' ' || c == '\'') continue;
                key.push_back((char)tolower((unsigned char)c));
            }

            for (uint16_t w = 0; w < repeat; ++w)
            {
                if (wordPos + 2 > buf.size()) { wordPos = buf.size(); break; }
                const uint16_t wordBytes = ReadU16(buf, wordPos); wordPos += 2;
                if (wordPos + wordBytes > buf.size()) { wordPos = buf.size(); break; }
                std::wstring word = ReadUtf16Z(buf, wordPos, wordBytes);
                wordPos += wordBytes;

                if (wordPos + 2 > buf.size()) { wordPos = buf.size(); break; }
                const uint16_t extBytes = ReadU16(buf, wordPos); wordPos += 2;
                if (wordPos + extBytes > buf.size()) { wordPos = buf.size(); break; }
                wordPos += extBytes;

                if (!word.empty() && !key.empty())
                    outPairs.emplace_back(key, word);
            }
        }

        if (outPairs.empty())
        {
            error = L"词库中没有可导入的词条";
            return false;
        }
        return true;
    }
} // namespace

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

    std::wstring path = dir + L"\\" + kDictSubdir + L"\\" + kDictFile;
    if (!FileExists(path))
    {
        path = dir + L"\\" + kDictFile;
        if (!FileExists(path)) return;
    }
    dbPath_ = path;

    // migrate schema if needed (requires write access)
    sqlite3* db = nullptr;
    const std::string dbPath8 = WideToUtf8(path);
    if (sqlite3_open_v2(dbPath8.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK)
    {
        ExecSql(db, "PRAGMA busy_timeout=2000;");
        EnsureSchema(db);
        sqlite3_close(db);
    }
    else
    {
        if (db) sqlite3_close(db);
    }

    LoadFromDatabase(path);
}

bool Dictionary::EnsureSchema(sqlite3* db)
{
    if (!TableExists(db, "libraries"))
    {
        // if entries has library_id maybe partial; but simplest: create and migrate
        if (!ExecSql(db,
            "CREATE TABLE IF NOT EXISTS libraries ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name TEXT NOT NULL,"
            " type INTEGER NOT NULL,"          // 0 mandarin, 1 cantonese
            " sort_order INTEGER NOT NULL DEFAULT 0"
            ");"))
            return false;
    }

    if (!ColumnExists(db, "entries", "library_id"))
    {
        if (!ExecSql(db, "ALTER TABLE entries ADD COLUMN library_id INTEGER;"))
            return false;
    }

    // 用户单次选词记忆：bit0=普通话模式，bit1=粤语模式
    if (!ColumnExists(db, "entries", "pin_modes"))
    {
        if (!ExecSql(db, "ALTER TABLE entries ADD COLUMN pin_modes INTEGER NOT NULL DEFAULT 0;"))
            return false;
    }

    MigrateLegacyIfNeeded(db);
    ExecSql(db, "CREATE INDEX IF NOT EXISTS entries_library_idx ON entries(library_id);");
    return true;
}

bool Dictionary::MigrateLegacyIfNeeded(sqlite3* db)
{
    // if no libraries exist, create defaults and assign entries
    sqlite3_stmt* stmt = nullptr;
    int libCount = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM libraries", -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW) libCount = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (libCount > 0)
    {
        // ensure default names exist? leave as-is
        // but assign any NULL library_id
        ExecSql(db, "BEGIN;");
        // find mandarin/cantonese default ids
        long long mandarinId = 0, cantoneseId = 0;
        if (sqlite3_prepare_v2(db, "SELECT id,type FROM libraries ORDER BY sort_order,id", -1, &stmt, nullptr) == SQLITE_OK)
        {
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                long long id = sqlite3_column_int64(stmt, 0);
                int type = sqlite3_column_int(stmt, 1);
                if (type == 0 && mandarinId == 0) mandarinId = id;
                if (type == 1 && cantoneseId == 0) cantoneseId = id;
            }
            sqlite3_finalize(stmt);
        }
        // create missing defaults
        if (mandarinId == 0)
        {
            sqlite3_stmt* ins = nullptr;
            sqlite3_prepare_v2(db, "INSERT INTO libraries(name,type,sort_order) VALUES(?1,0,0)", -1, &ins, nullptr);
            sqlite3_bind_text(ins, 1, WideToUtf8(L"默认普通话词库").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ins); sqlite3_finalize(ins);
            mandarinId = sqlite3_last_insert_rowid(db);
        }
        if (cantoneseId == 0)
        {
            sqlite3_stmt* ins = nullptr;
            sqlite3_prepare_v2(db, "INSERT INTO libraries(name,type,sort_order) VALUES(?1,1,0)", -1, &ins, nullptr);
            sqlite3_bind_text(ins, 1, WideToUtf8(L"默认粤语词库").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ins); sqlite3_finalize(ins);
            cantoneseId = sqlite3_last_insert_rowid(db);
        }
        // assign nulls by source
        sqlite3_stmt* upd = nullptr;
        if (sqlite3_prepare_v2(db, "UPDATE entries SET library_id=?1 WHERE library_id IS NULL AND source=0", -1, &upd, nullptr)==SQLITE_OK)
        { sqlite3_bind_int64(upd,1,mandarinId); sqlite3_step(upd); sqlite3_finalize(upd); }
        if (sqlite3_prepare_v2(db, "UPDATE entries SET library_id=?1 WHERE library_id IS NULL AND source=1", -1, &upd, nullptr)==SQLITE_OK)
        { sqlite3_bind_int64(upd,1,cantoneseId); sqlite3_step(upd); sqlite3_finalize(upd); }
        // ensure names of first defaults if empty? update to defaults for migration naming requirement
        ExecSql(db, "COMMIT;");
        return true;
    }

    ExecSql(db, "BEGIN;");

    // create default libraries with required names
    long long mandarinId = 0, cantoneseId = 0;
    {
        sqlite3_stmt* ins = nullptr;
        sqlite3_prepare_v2(db, "INSERT INTO libraries(name,type,sort_order) VALUES(?1,0,0)", -1, &ins, nullptr);
        sqlite3_bind_text(ins, 1, WideToUtf8(L"默认普通话词库").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ins); sqlite3_finalize(ins);
        mandarinId = sqlite3_last_insert_rowid(db);
    }
    {
        sqlite3_stmt* ins = nullptr;
        sqlite3_prepare_v2(db, "INSERT INTO libraries(name,type,sort_order) VALUES(?1,1,0)", -1, &ins, nullptr);
        sqlite3_bind_text(ins, 1, WideToUtf8(L"默认粤语词库").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ins); sqlite3_finalize(ins);
        cantoneseId = sqlite3_last_insert_rowid(db);
    }

    {
        sqlite3_stmt* upd = nullptr;
        sqlite3_prepare_v2(db, "UPDATE entries SET library_id=?1 WHERE source=0", -1, &upd, nullptr);
        sqlite3_bind_int64(upd, 1, mandarinId);
        sqlite3_step(upd); sqlite3_finalize(upd);
    }
    {
        sqlite3_stmt* upd = nullptr;
        sqlite3_prepare_v2(db, "UPDATE entries SET library_id=?1 WHERE source=1", -1, &upd, nullptr);
        sqlite3_bind_int64(upd, 1, cantoneseId);
        sqlite3_step(upd); sqlite3_finalize(upd);
    }

    ExecSql(db, "COMMIT;");
    return true;
}

bool Dictionary::OpenWritableDb(sqlite3** outDb, std::wstring& outPath) const
{
    *outDb = nullptr;
    outPath = dbPath_;
    if (outPath.empty())
    {
        const std::wstring dir = GetModuleDir();
        if (!dir.empty()) outPath = dir + L"\\" + kDictSubdir + L"\\" + kDictFile;
    }
    if (outPath.empty()) return false;
    const std::string dbPath8 = WideToUtf8(outPath);
    if (sqlite3_open_v2(dbPath8.c_str(), outDb, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
    {
        if (*outDb) { sqlite3_close(*outDb); *outDb = nullptr; }
        return false;
    }
    ExecSql(*outDb, "PRAGMA busy_timeout=2000;");
    return true;
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

    sqlite3_stmt* probe = nullptr;
    const bool hasTrad =
        sqlite3_prepare_v2(db, "SELECT word_trad FROM entries LIMIT 1", -1, &probe, nullptr) == SQLITE_OK;
    if (probe) sqlite3_finalize(probe);
    const bool hasLib = TableExists(db, "libraries") && ColumnExists(db, "entries", "library_id");
    const bool hasPin = ColumnExists(db, "entries", "pin_modes");

    // 旧词库（或只读加载时未完成迁移）可能没有 pin_modes，做兼容处理
    const std::string pinCol = hasPin ? ", pin_modes" : "";
    const std::string pinColE = hasPin ? ", e.pin_modes" : "";

    sqlite3_stmt* stmt = nullptr;
    std::string sql;
    if (hasLib && hasTrad)
    {
        sql = "SELECT e.id, e.word, e.word_trad, e.pinyin, e.initials, e.syl_lens, e.weight, e.source,"
              " e.nasal_only, e.valid_pinyin, COALESCE(l.sort_order,0) AS lib_prio, COALESCE(l.type,e.source) AS lib_type" +
              pinColE +
              " FROM entries e LEFT JOIN libraries l ON e.library_id = l.id"
              " ORDER BY e.id";
    }
    else if (hasLib)
    {
        sql = "SELECT e.id, e.word, e.pinyin, e.initials, e.syl_lens, e.weight, e.source,"
              " e.nasal_only, e.valid_pinyin, COALESCE(l.sort_order,0) AS lib_prio, COALESCE(l.type,e.source) AS lib_type" +
              pinColE +
              " FROM entries e LEFT JOIN libraries l ON e.library_id = l.id"
              " ORDER BY e.id";
    }
    else if (hasTrad)
    {
        sql = "SELECT id, word, word_trad, pinyin, initials, syl_lens, weight, source, nasal_only, valid_pinyin" +
              pinCol + " FROM entries ORDER BY id";
    }
    else
    {
        sql = "SELECT id, word, pinyin, initials, syl_lens, weight, source, nasal_only, valid_pinyin" +
              pinCol + " FROM entries ORDER BY id";
    }

    size_t added = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
    {
        entries_.clear();
        entries_.reserve(70000);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            int col = 0;
            const long long id = sqlite3_column_int64(stmt, col++);
            const unsigned char* word8 = sqlite3_column_text(stmt, col++);
            if (!word8) continue;

            std::wstring wordTrad;
            if (hasTrad)
            {
                const unsigned char* trad8 = sqlite3_column_text(stmt, col++);
                if (trad8) wordTrad = Utf8ToWide(reinterpret_cast<const char*>(trad8));
            }

            const unsigned char* pinyin8 = sqlite3_column_text(stmt, col++);
            const unsigned char* initials8 = sqlite3_column_text(stmt, col++);
            if (!pinyin8 || !initials8) continue;

            std::wstring word = Utf8ToWide(reinterpret_cast<const char*>(word8));
            if (word.empty()) continue;

            const void* blob = sqlite3_column_blob(stmt, col++);
            const int blobLen = sqlite3_column_bytes(stmt, col - 1);
            std::string sylLens;
            if (blob && blobLen > 0)
                sylLens.assign(static_cast<const char*>(blob), (size_t)blobLen);

            const int weight = sqlite3_column_int(stmt, col++);
            const int source = sqlite3_column_int(stmt, col++);
            const int nasalOnly = sqlite3_column_int(stmt, col++);
            const int validPinyin = sqlite3_column_int(stmt, col++);

            int libPrio = 0;
            int libType = source;
            if (hasLib)
            {
                libPrio = sqlite3_column_int(stmt, col++);
                libType = sqlite3_column_int(stmt, col++);
            }

            const int pinModes = hasPin ? sqlite3_column_int(stmt, col++) : 0;

            DictEntry e;
            e.id = id;
            e.word = std::move(word);
            e.wordTrad = std::move(wordTrad);
            e.pinyin = reinterpret_cast<const char*>(pinyin8);
            e.initials = reinterpret_cast<const char*>(initials8);
            e.sylLens = std::move(sylLens);
            e.weight = weight;
            e.source = (libType == 1) ? LangSource::Cantonese : LangSource::Mandarin;
            e.libPriority = libPrio;
            e.nasalOnly = nasalOnly != 0;
            e.validPinyin = validPinyin != 0;
            e.pinModes = pinModes;
            entries_.push_back(std::move(e));
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
    DictEntry e;
    e.word = word;
    e.pinyin = pinyin;
    e.initials = BuildInitials(syllables);
    e.sylLens = EncodeSyllableLengths(syllables);
    e.weight = weight;
    e.source = source;
    e.nasalOnly = IsNasalOnlyPinyin(pinyin);
    e.validPinyin = IsValidPinyinSequence(pinyin);
    entries_.push_back(std::move(e));
}

bool Dictionary::AddUserCustomWordIfMissing(const std::wstring& word,
                                            const std::string& pinyin,
                                            std::wstring& error)
{
    error.clear();
    EnsureLoaded();
    if (word.empty() || pinyin.empty()) return false;

    // entries_ 已包含当前所有词库的词条；按拼音精确匹配判断该词组是否已存在
    for (const auto& e : entries_)
    {
        if (e.pinyin == pinyin) return false;
    }

    sqlite3* db = nullptr;
    std::wstring path;
    if (!OpenWritableDb(&db, path)) { error = L"无法打开词库数据库"; return false; }
    EnsureSchema(db);

    // 先查有没有「用户自定义词库」，没有就新建一个（按普通话词库处理，
    // 使其出现在词库管理列表并参与同一套候选排序）
    long long libId = 0;
    sqlite3_stmt* stmt = nullptr;
    const std::string libName = WideToUtf8(kUserCustomLibName);
    if (sqlite3_prepare_v2(db,
            "SELECT id FROM libraries WHERE name=?1 ORDER BY id LIMIT 1",
            -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, libName.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) libId = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }

    ExecSql(db, "BEGIN;");
    if (libId == 0)
    {
        int maxOrder = -1;
        stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT COALESCE(MAX(sort_order),-1) FROM libraries WHERE type=0",
                -1, &stmt, nullptr) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW) maxOrder = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                "INSERT INTO libraries(name,type,sort_order) VALUES(?1,0,?2)",
                -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, libName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, maxOrder + 1);
            if (sqlite3_step(stmt) == SQLITE_DONE) libId = sqlite3_last_insert_rowid(db);
            sqlite3_finalize(stmt);
        }
        if (libId == 0)
        {
            ExecSql(db, "ROLLBACK;");
            sqlite3_close(db);
            error = L"创建用户自定义词库失败";
            return false;
        }
    }

    // 写入词条；各派生列与导入词库时的算法保持一致
    const auto syllables = SplitSyllables(pinyin, pinyin);
    const std::string initials = BuildInitials(syllables);
    const std::string sylLens = EncodeSyllableLengths(syllables);
    const std::string wordUtf8 = WideToUtf8(word);

    stmt = nullptr;
    const char* sql =
        "INSERT INTO entries(word,word_trad,pinyin,initials,syl_lens,weight,source,"
        "nasal_only,valid_pinyin,library_id)"
        " VALUES(?1,?1,?2,?3,?4,?5,0,?6,?7,?8)";
    bool inserted = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, wordUtf8.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pinyin.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, initials.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 4, sylLens.data(), (int)sylLens.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, 1000000);
        sqlite3_bind_int(stmt, 6, IsNasalOnlyPinyin(pinyin) ? 1 : 0);
        sqlite3_bind_int(stmt, 7, IsValidPinyinSequence(pinyin) ? 1 : 0);
        sqlite3_bind_int64(stmt, 8, libId);
        inserted = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    ExecSql(db, inserted ? "COMMIT;" : "ROLLBACK;");
    sqlite3_close(db);

    if (!inserted) { error = L"写入用户自定义词库失败"; return false; }
    Reload();
    return true;
}

bool Dictionary::PromoteSingleChoice(const DictEntry& entry,
                                     const std::string& queryPinyin, int mode)
{
    EnsureLoaded();
    if (entry.id <= 0 || entry.pinyin.empty() || queryPinyin.empty()) return false;

    // 只影响当前输入模式，避免普通话/粤语模式的个人偏好互相干扰
    const int bit = (mode == 1) ? 2 : 1;

    sqlite3* db = nullptr;
    std::wstring path;
    if (!OpenWritableDb(&db, path)) return false;
    EnsureSchema(db);

    // 权重提升到同拼音（以及该词条自身拼音）下的最高权重之上；
    // 同时设一个下限，使其高于用户自定义词条的固定权重。
    int maxWeight = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT MAX(weight) FROM entries WHERE pinyin=?1 OR pinyin=?2",
            -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, queryPinyin.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, entry.pinyin.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) maxWeight = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    int newWeight = maxWeight + 1;
    if (newWeight < 1000001) newWeight = 1000001;

    bool ok = true;
    ExecSql(db, "BEGIN;");

    // 先清掉同一查询拼音下其它词条的该模式置顶标记（最后一次选择生效）
    stmt = nullptr;
    if (sqlite3_prepare_v2(db,
            "UPDATE entries SET pin_modes = pin_modes & ?1 WHERE pinyin=?2",
            -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, ~bit);
        sqlite3_bind_text(stmt, 2, queryPinyin.c_str(), -1, SQLITE_TRANSIENT);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    else ok = false;

    // 再给选中的词条提权并置顶。用主键定位，繁简字形转换不会影响匹配。
    if (ok)
    {
        stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                "UPDATE entries SET weight=?1, pin_modes=pin_modes|?2 WHERE id=?3",
                -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, newWeight);
            sqlite3_bind_int(stmt, 2, bit);
            sqlite3_bind_int64(stmt, 3, entry.id);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        }
        else ok = false;
    }

    ExecSql(db, ok ? "COMMIT;" : "ROLLBACK;");
    sqlite3_close(db);
    if (!ok) return false;

    // 同步内存中的词条，避免为一次选词重载整本词库
    for (DictEntry& e : entries_)
    {
        if (e.pinyin == queryPinyin)
            e.pinModes &= ~bit;
        if (e.id == entry.id)
        {
            e.weight = newWeight;
            e.pinModes |= bit;
        }
    }
    return true;
}

void Dictionary::Reload()
{
    entries_.clear();
    loaded_ = false;
    EnsureLoaded();
}

std::vector<MandarinLibraryInfo> Dictionary::GetMandarinLibraries() const
{
    std::vector<MandarinLibraryInfo> libs;
    sqlite3* db = nullptr;
    std::wstring path;
    // open readonly
    path = dbPath_;
    if (path.empty())
    {
        const std::wstring dir = GetModuleDir();
        if (!dir.empty()) path = dir + L"\\" + kDictSubdir + L"\\" + kDictFile;
    }
    if (path.empty()) return libs;
    if (sqlite3_open_v2(WideToUtf8(path).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return libs;
    }
    if (!TableExists(db, "libraries")) { sqlite3_close(db); return libs; }

    const char* sql =
        "SELECT l.id, l.name, l.sort_order, COUNT(e.id) "
        "FROM libraries l LEFT JOIN entries e ON e.library_id=l.id "
        "WHERE l.type=0 "
        "GROUP BY l.id, l.name, l.sort_order "
        "ORDER BY l.sort_order ASC, l.id ASC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            MandarinLibraryInfo info;
            info.id = sqlite3_column_int64(stmt, 0);
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            info.name = name ? Utf8ToWide(reinterpret_cast<const char*>(name)) : L"";
            info.sortOrder = sqlite3_column_int(stmt, 2);
            info.wordCount = sqlite3_column_int(stmt, 3);
            libs.push_back(std::move(info));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return libs;
}

std::vector<LibraryWordEntry> Dictionary::GetLibraryWords(long long id) const
{
    std::vector<LibraryWordEntry> words;
    std::wstring path = dbPath_;
    if (path.empty())
    {
        const std::wstring dir = GetModuleDir();
        if (!dir.empty()) path = dir + L"\\" + kDictSubdir + L"\\" + kDictFile;
    }
    if (path.empty() || id <= 0) return words;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(WideToUtf8(path).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return words;
    }
    if (!TableExists(db, "libraries") || !ColumnExists(db, "entries", "library_id"))
    {
        sqlite3_close(db);
        return words;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, word, pinyin FROM entries WHERE library_id=?1 ORDER BY id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, id);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const long long entryId = sqlite3_column_int64(stmt, 0);
            const unsigned char* word8 = sqlite3_column_text(stmt, 1);
            const unsigned char* py8 = sqlite3_column_text(stmt, 2);
            if (!word8 || !py8) continue;

            LibraryWordEntry item;
            item.id = entryId;
            item.word = Utf8ToWide(reinterpret_cast<const char*>(word8));
            item.pinyin = Utf8ToWide(reinterpret_cast<const char*>(py8));
            if (!item.word.empty()) words.push_back(std::move(item));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return words;
}

bool Dictionary::MoveMandarinLibrary(long long id, int direction)
{
    sqlite3* db = nullptr; std::wstring path;
    if (!OpenWritableDb(&db, path)) return false;

    // load ordered mandarin libs
    std::vector<long long> ids;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id FROM libraries WHERE type=0 ORDER BY sort_order ASC, id ASC", -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW) ids.push_back(sqlite3_column_int64(stmt, 0));
        sqlite3_finalize(stmt);
    }
    int idx = -1;
    for (int i = 0; i < (int)ids.size(); ++i) if (ids[i] == id) { idx = i; break; }
    if (idx < 0) { sqlite3_close(db); return false; }
    int j = idx + (direction < 0 ? -1 : 1);
    if (j < 0 || j >= (int)ids.size()) { sqlite3_close(db); return false; }
    std::swap(ids[idx], ids[j]);

    ExecSql(db, "BEGIN;");
    for (int i = 0; i < (int)ids.size(); ++i)
    {
        sqlite3_stmt* upd = nullptr;
        sqlite3_prepare_v2(db, "UPDATE libraries SET sort_order=?1 WHERE id=?2", -1, &upd, nullptr);
        sqlite3_bind_int(upd, 1, i);
        sqlite3_bind_int64(upd, 2, ids[i]);
        sqlite3_step(upd); sqlite3_finalize(upd);
    }
    ExecSql(db, "COMMIT;");
    sqlite3_close(db);
    Reload();
    return true;
}

bool Dictionary::DeleteMandarinLibrary(long long id, std::wstring& error)
{
    sqlite3* db = nullptr; std::wstring path;
    if (!OpenWritableDb(&db, path)) { error = L"无法打开词库数据库"; return false; }

    // ensure it's mandarin and not the last one
    int mandarinCount = 0; int type = -1;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM libraries WHERE type=0", -1, &stmt, nullptr)==SQLITE_OK)
    { if (sqlite3_step(stmt)==SQLITE_ROW) mandarinCount = sqlite3_column_int(stmt,0); sqlite3_finalize(stmt); }
    if (sqlite3_prepare_v2(db, "SELECT type FROM libraries WHERE id=?1", -1, &stmt, nullptr)==SQLITE_OK)
    {
        sqlite3_bind_int64(stmt,1,id);
        if (sqlite3_step(stmt)==SQLITE_ROW) type = sqlite3_column_int(stmt,0);
        sqlite3_finalize(stmt);
    }
    if (type != 0) { sqlite3_close(db); error = L"只能删除普通话词库"; return false; }
    if (mandarinCount <= 1) { sqlite3_close(db); error = L"至少要保留一个普通话词库"; return false; }

    ExecSql(db, "BEGIN;");
    sqlite3_stmt* del = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM entries WHERE library_id=?1", -1, &del, nullptr);
    sqlite3_bind_int64(del,1,id); sqlite3_step(del); sqlite3_finalize(del);
    sqlite3_prepare_v2(db, "DELETE FROM libraries WHERE id=?1", -1, &del, nullptr);
    sqlite3_bind_int64(del,1,id); sqlite3_step(del); sqlite3_finalize(del);
    ExecSql(db, "COMMIT;");
    sqlite3_close(db);
    Reload();
    return true;
}

bool Dictionary::DeleteLibraryEntries(long long libraryId, const std::vector<long long>& entryIds, std::wstring& error)
{
    error.clear();
    if (libraryId <= 0 || entryIds.empty()) return true;

    sqlite3* db = nullptr;
    std::wstring path;
    if (!OpenWritableDb(&db, path)) { error = L"无法打开词库数据库"; return false; }
    EnsureSchema(db);

    // Verify library exists and is mandarin (user custom is mandarin).
    int type = -1;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT type FROM libraries WHERE id=?1", -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, libraryId);
        if (sqlite3_step(stmt) == SQLITE_ROW) type = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (type != 0)
    {
        sqlite3_close(db);
        error = L"只能删除普通话词库中的词条";
        return false;
    }

    ExecSql(db, "BEGIN;");
    sqlite3_stmt* del = nullptr;
    bool ok = (sqlite3_prepare_v2(db,
        "DELETE FROM entries WHERE id=?1 AND library_id=?2", -1, &del, nullptr) == SQLITE_OK);
    if (ok)
    {
        for (long long entryId : entryIds)
        {
            if (entryId <= 0) continue;
            sqlite3_reset(del);
            sqlite3_clear_bindings(del);
            sqlite3_bind_int64(del, 1, entryId);
            sqlite3_bind_int64(del, 2, libraryId);
            if (sqlite3_step(del) != SQLITE_DONE)
            {
                ok = false;
                break;
            }
        }
        sqlite3_finalize(del);
    }

    if (ok) ExecSql(db, "COMMIT;");
    else ExecSql(db, "ROLLBACK;");
    sqlite3_close(db);

    if (!ok)
    {
        error = L"删除词条失败";
        return false;
    }

    Reload();
    return true;
}

bool Dictionary::ImportQcelLibrary(const std::wstring& qcelPath, std::wstring& outName, std::wstring& error)
{
    std::vector<std::pair<std::string, std::wstring>> pairs;
    if (!ParseQcel(qcelPath, outName, pairs, error)) return false;

    sqlite3* db = nullptr; std::wstring path;
    if (!OpenWritableDb(&db, path)) { error = L"无法打开词库数据库"; return false; }
    EnsureSchema(db);

    // determine sort_order (append after last mandarin)
    int maxOrder = -1;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(sort_order),-1) FROM libraries WHERE type=0", -1, &stmt, nullptr)==SQLITE_OK)
    { if (sqlite3_step(stmt)==SQLITE_ROW) maxOrder = sqlite3_column_int(stmt,0); sqlite3_finalize(stmt); }

    ExecSql(db, "BEGIN;");
    long long libId = 0;
    sqlite3_stmt* insLib = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT INTO libraries(name,type,sort_order) VALUES(?1,0,?2)", -1, &insLib, nullptr)==SQLITE_OK)
    {
        sqlite3_bind_text(insLib, 1, WideToUtf8(outName).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insLib, 2, maxOrder + 1);
        if (sqlite3_step(insLib)==SQLITE_DONE) libId = sqlite3_last_insert_rowid(db);
        sqlite3_finalize(insLib);
    }
    if (libId == 0) { ExecSql(db,"ROLLBACK;"); sqlite3_close(db); error=L"创建词库失败"; return false; }

    sqlite3_stmt* ins = nullptr;
    const char* sql =
        "INSERT INTO entries(word,word_trad,pinyin,initials,syl_lens,weight,source,nasal_only,valid_pinyin,library_id)"
        " VALUES(?1,'',?2,?3,?4,?5,0,?6,?7,?8)";
    if (sqlite3_prepare_v2(db, sql, -1, &ins, nullptr) != SQLITE_OK)
    { ExecSql(db,"ROLLBACK;"); sqlite3_close(db); error=L"准备写入词条失败"; return false; }

    // weight: keep import order reasonable
    int baseWeight = 1000000;
    for (size_t i = 0; i < pairs.size(); ++i)
    {
        const auto& p = pairs[i];
        const auto syllables = SplitSyllables(p.first, p.first);
        const std::string initials = BuildInitials(syllables);
        const std::string syl = EncodeSyllableLengths(syllables);
        const int weight = baseWeight - (int)i;
        sqlite3_reset(ins);
        sqlite3_bind_text(ins, 1, WideToUtf8(p.second).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, p.first.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, initials.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(ins, 4, syl.data(), (int)syl.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 5, weight);
        sqlite3_bind_int(ins, 6, IsNasalOnlyPinyin(p.first) ? 1 : 0);
        sqlite3_bind_int(ins, 7, IsValidPinyinSequence(p.first) ? 1 : 0);
        sqlite3_bind_int64(ins, 8, libId);
        sqlite3_step(ins);
    }
    sqlite3_finalize(ins);
    ExecSql(db, "COMMIT;");
    sqlite3_close(db);

    Reload();
    return true;
}

bool Dictionary::ResetDictionaryFromBackup(std::wstring& error)
{
    error.clear();
    EnsureLoaded();
    if (dbPath_.empty())
    {
        error = L"未找到当前词库数据库";
        return false;
    }

    // 备份库与当前库放在同一目录（安装后为 {app}\dicts\）
    const size_t slash = dbPath_.find_last_of(L"\\/");
    const std::wstring backupPath =
        (slash == std::wstring::npos ? std::wstring() : dbPath_.substr(0, slash + 1)) + kBackupFile;
    if (!FileExists(backupPath))
    {
        error = L"找不到备份词库文件：\n" + backupPath;
        return false;
    }

    sqlite3* db = nullptr;
    std::wstring currentPath;
    if (!OpenWritableDb(&db, currentPath))
    {
        error = L"无法打开当前词库数据库：\n" + currentPath;
        return false;
    }

    auto hasBackupTable = [db](const char* table)
    {
        const std::string sql = std::string("SELECT 1 FROM ") + kBackupSchema + "." + table + " LIMIT 1";
        sqlite3_stmt* probe = nullptr;
        const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &probe, nullptr);
        if (probe) sqlite3_finalize(probe);
        return rc == SQLITE_OK;
    };

    bool attached = false;
    bool inTransaction = false;
    bool ok = false;
    std::string err8;
    do
    {
        // 先把当前库升级到最新 schema，再按两边共有的列复制，兼容旧版备份库
        if (!EnsureSchema(db)) { err8 = sqlite3_errmsg(db); break; }

        const std::string backup8 = WideToUtf8(backupPath);
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, "ATTACH DATABASE ?1 AS m2c_bak", -1, &stmt, nullptr);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, backup8.c_str(), -1, SQLITE_TRANSIENT);
        if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
        if (stmt) sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) { err8 = sqlite3_errmsg(db); break; }
        attached = true;

        if (!TableExists(db, "entries") || !TableExists(db, "libraries") ||
            !hasBackupTable("entries") || !hasBackupTable("libraries"))
        {
            err8 = "当前词库或备份词库缺少 entries/libraries 表";
            break;
        }

        const auto mainEntries = TableColumns(db, "main", "entries");
        const auto mainLibs    = TableColumns(db, "main", "libraries");
        const auto bakEntries  = TableColumns(db, kBackupSchema, "entries");
        const auto bakLibs     = TableColumns(db, kBackupSchema, "libraries");
        const std::string copyEntries = MakeCopySql(mainEntries, bakEntries, "entries");
        const std::string copyLibs    = MakeCopySql(mainLibs, bakLibs, "libraries");
        if (copyEntries.empty() || copyLibs.empty())
        {
            err8 = "当前词库与备份词库的表结构不兼容";
            break;
        }

        if (!ExecSql(db, "BEGIN IMMEDIATE;")) { err8 = sqlite3_errmsg(db); break; }
        inTransaction = true;

        // 先清空词库相关表（顺序：词条 -> 词库），再整表导入备份数据
        if (!ExecSql(db, "DELETE FROM entries;") ||
            !ExecSql(db, "DELETE FROM libraries;"))
        { err8 = sqlite3_errmsg(db); break; }
        // 清掉 AUTOINCREMENT 计数，避免恢复后新建词库的 id 与备份数据冲突
        if (TableExists(db, "sqlite_sequence") &&
            !ExecSql(db, "DELETE FROM sqlite_sequence WHERE name='libraries';"))
        { err8 = sqlite3_errmsg(db); break; }

        if (!ExecSql(db, copyLibs.c_str()) || !ExecSql(db, copyEntries.c_str()))
        { err8 = sqlite3_errmsg(db); break; }
        // 选词记忆属于用户数据，恢复出厂词库时一并清除
        if (!ExecSql(db, "UPDATE entries SET pin_modes=0;"))
        { err8 = sqlite3_errmsg(db); break; }

        if (!ExecSql(db, "COMMIT;")) { err8 = sqlite3_errmsg(db); break; }
        inTransaction = false;

        // 若备份库较旧、缺少 library_id 等列，这里补建默认值并回填归属词库
        if (!EnsureSchema(db)) { err8 = sqlite3_errmsg(db); break; }
        ok = true;
    } while (false);

    if (!ok && inTransaction) ExecSql(db, "ROLLBACK;");
    if (attached) ExecSql(db, "DETACH DATABASE m2c_bak;");
    sqlite3_close(db);

    if (!ok)
    {
        error = L"重置词库失败：";
        if (!err8.empty()) error += Utf8ToWide(err8);
        else error += L"未知错误";
        return false;
    }

    Reload();
    return true;
}

std::vector<DictEntry> Dictionary::QueryOnce(const std::string& pinyin, int mode,
                                             const std::string& initialsQuery) const
{
    std::vector<DictEntry> result;
    if (pinyin.empty()) return result;

    const size_t plen = pinyin.size();
    const size_t qlen = initialsQuery.size();

    for (const auto& e : entries_)
    {
        const size_t elen = e.pinyin.size();
        bool pinyinMatch = false;
        if (elen >= plen)
            pinyinMatch = (e.pinyin.compare(0, plen, pinyin) == 0);
        else
            pinyinMatch = (pinyin.compare(0, elen, e.pinyin) == 0);

        if (pinyinMatch && elen < plen && e.nasalOnly)
            continue;

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
                initialsFull = (e.initials.size() == qlen);
                initialsPrefix = !initialsFull;
            }
        }
        const bool initialsMatch = initialsFull || initialsPrefix;

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

        int rank = 0;
        if (pinyinMatch)
        {
            if (elen == plen) rank = e.validPinyin ? 0 : 5;
            else if (e.nasalOnly || !e.validPinyin) rank = 5;
            else if (mixedFull) rank = 1;
            else if (CoveredSyllableCount(e.sylLens, plen) >= 2) rank = 1;
            else rank = (e.word.size() == 1) ? 2 : 3;
        }
        else if (initialsFull || mixedFull)
            rank = 1;
        else if (initialsPrefix)
            rank = 4;
        else
            rank = 3;

        size_t matchedLen = 0;
        if (mixedFull)         matchedLen = plen;
        else if (pinyinMatch)  matchedLen = (std::min)(plen, elen);
        else if (initialsFull) matchedLen = RuleConsume(e, initialsQuery);

        result.push_back(e);
        DictEntry& added = result.back();
        added.viaInitials = (initialsMatch || mixedFull || mixedPartial);
        added.fullWordMatch = (mixedFull || initialsFull || (pinyinMatch && elen <= plen));
        added.matchRank = rank;
        added.matchedLen = matchedLen;
    }

    // 输入串能表示的字符个数（声母个数）。候选词的字数不能超过它，
    // 例如 z / zi 只能显示单字，zl / zil / zili 才显示双字词。
    const size_t inputInitials = CountInputInitials(initialsQuery.empty() ? pinyin : initialsQuery);
    const int modeBit = (mode == 1) ? 2 : 1;

    std::stable_sort(result.begin(), result.end(),
        [&](const DictEntry& a, const DictEntry& b)
        {
            // 用户单次选词记忆优先：该模式下被置顶的词条排最前。
            // 但置顶不能违背字数规则：字数超过输入可表示字符数的词条不参与置顶。
            const bool pa = (a.pinModes & modeBit) != 0 && a.word.size() <= inputInitials;
            const bool pb = (b.pinModes & modeBit) != 0 && b.word.size() <= inputInitials;
            if (pa != pb) return pa;

            if (a.matchRank != b.matchRank) return a.matchRank < b.matchRank;
            if (a.matchedLen != b.matchedLen) return a.matchedLen > b.matchedLen;

            // 当前模式优先的来源（普通话模式先普通话，粤语模式先粤语）
            const int ta = SourceTier(a, mode);
            const int tb = SourceTier(b, mode);
            if (ta != tb) return ta > tb;

            // 同为普通话词库时，按用户在词库管理中的排序（sort_order 越小越优先）
            if (a.source == LangSource::Mandarin && b.source == LangSource::Mandarin &&
                a.libPriority != b.libPriority)
                return a.libPriority < b.libPriority;

            if (a.pinyin.size() != b.pinyin.size())
                return a.pinyin.size() < b.pinyin.size();
            return a.weight > b.weight;
        });

    if (inputInitials < 2)
    {
        // 一个声母只能表示一个汉字：z / zi 等输入只保留单字候选
        result.erase(std::remove_if(result.begin(), result.end(),
            [inputInitials](const DictEntry& e)
            {
                return e.word.size() > inputInitials;
            }),
            result.end());
    }
    else
    {
        result.erase(std::remove_if(result.begin(), result.end(),
            [inputInitials, plen](const DictEntry& e)
            {
                return !e.fullWordMatch &&
                       e.pinyin.size() > plen &&
                       e.word.size() > inputInitials;
            }),
            result.end());
    }

    return result;
}

std::vector<DictEntry> Dictionary::GetCandidates(const std::string& pinyin, int mode,
                                                 const std::string& initialsQuery) const
{
    std::vector<DictEntry> result = QueryOnce(pinyin, mode, initialsQuery);
    // 一个声母只能表示一个汉字：z / zi 等输入不再进入下面的多字候选补充逻辑
    if (CountInputInitials(initialsQuery.empty() ? pinyin : initialsQuery) < 2) return result;

    const bool hasWordGroup = std::any_of(result.begin(), result.end(),
        [](const DictEntry& e) { return e.word.size() >= 2; });

    if (hasWordGroup)
    {
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

    const std::string& input = initialsQuery.empty() ? pinyin : initialsQuery;

    std::vector<DictEntry> all;
    for (const auto& e : entries_)
    {
        const size_t covered = RuleConsume(e, input);
        if (covered == 0) continue;
        if (e.word.size() >= 2 && covered < 2) continue;

        DictEntry added = e;
        added.viaInitials = true;
        added.fullWordMatch = true;
        added.matchedLen = covered;
        added.consumeInputLen = covered;
        all.push_back(std::move(added));
    }

    const size_t inputInitials = CountInputInitials(initialsQuery.empty() ? pinyin : initialsQuery);
    const int modeBit = (mode == 1) ? 2 : 1;
    std::stable_sort(all.begin(), all.end(),
        [&](const DictEntry& a, const DictEntry& b)
        {
            const bool pa = (a.pinModes & modeBit) != 0 && a.word.size() <= inputInitials;
            const bool pb = (b.pinModes & modeBit) != 0 && b.word.size() <= inputInitials;
            if (pa != pb) return pa;

            if (a.matchedLen != b.matchedLen) return a.matchedLen > b.matchedLen;

            const int ta = SourceTier(a, mode);
            const int tb = SourceTier(b, mode);
            if (ta != tb) return ta > tb;

            if (a.source == LangSource::Mandarin && b.source == LangSource::Mandarin &&
                a.libPriority != b.libPriority)
                return a.libPriority < b.libPriority;
            return a.weight > b.weight;
        });

    result.clear();
    std::set<std::pair<std::wstring, int>> seen;
    result.reserve(all.size());
    for (DictEntry& e : all)
    {
        if (!seen.emplace(e.word, (int)e.source).second) continue;
        result.push_back(std::move(e));
    }

    return result;
}

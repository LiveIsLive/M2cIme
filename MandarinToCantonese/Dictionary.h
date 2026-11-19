#pragma once

#include <cstddef>
#include <string>
#include <vector>

// 词来源：普通话词库 / 粤语词库
enum class LangSource
{
    Mandarin,
    Cantonese
};

// 词典条目
struct DictEntry
{
    std::wstring word;     // 汉字（简体，或词库原始写法）
    std::wstring wordTrad; // 对应的繁体写法（词库中预生成；旧词库可能为空）
    std::string  pinyin;   // 连续拼音（小写，无声调，如 "nihao"）
    std::string  initials; // 各音节声母拼接（如 "nihao" -> "nh"）
    std::string  sylLens;  // 各音节长度（每字节一个音节长度，"nihao" -> {2,3}），用于混拼匹配
    int          weight;   // 词频权重（越大越靠前）
    LangSource   source;   // 来源词库
    int          libPriority = 0; // 普通话词库优先级（数值越小越优先，仅同为普通话时比较）
    bool         nasalOnly = false;   // 首音节是鼻音自成音节（n/ng/m/hm/hng，如「嗯」「呒」），没有真正声母
    bool         validPinyin = true;  // 拼音能否完整切分为合法音节（"nihao"=ni+hao 为 true；"n" 为 false）
    bool         viaInitials = false; // 查询结果标记：是否靠声母简拼 / 混拼匹配（词库本身不使用）
    bool         fullWordMatch = false; // 查询结果标记：词条所有音节是否都被输入完整匹配（词库本身不使用）
    int          matchRank = 0;       // 查询结果标记：匹配优先级（越小越靠前，词库本身不使用）
    size_t       matchedLen = 0;      // 查询结果标记：本次实际覆盖的输入字母数（用于更长覆盖优先）
    size_t       consumeInputLen = 0; // 查询结果标记：选中后消耗组合串的字符数（回退候选专用，0 = 按默认规则消耗）
};

// 普通话词库信息（用于词库管理界面）
struct MandarinLibraryInfo
{
    long long    id = 0;
    std::wstring name;
    int          wordCount = 0;
    int          sortOrder = 0; // 越小越优先（列表中排在前面）
};

// 词库浏览用的一条“字词 + 拼音”（已转成宽字符，方便界面直接显示）
struct LibraryWordEntry
{
    std::wstring word;
    std::wstring pinyin;
};

// 词典：从外置 SQLite 词库文件加载普通话 + 粤语词条，
// 并根据当前输入模式做候选词优先级排序。
//
// 词库文件位于 DLL 所在目录（安装目录）下的 dicts 子目录：
//   dicts\m2c_dict.db
// 若 dicts 子目录不存在，则回退查找 DLL 所在目录本身。
class Dictionary
{
public:
    static Dictionary& Instance();

    // 加载外置词库（首次访问自动加载）
    void EnsureLoaded();

    // 按拼音（连续串）查找候选词。
    std::vector<DictEntry> GetCandidates(const std::string& pinyin, int mode,
                                         const std::string& initialsQuery = std::string()) const;

    // 直接添加一条词条（供扩展/测试使用）
    void AddEntry(const std::wstring& word, const std::string& pinyin,
                  int weight, LangSource source);

    // ===== 普通话词库管理 =====
    std::vector<MandarinLibraryInfo> GetMandarinLibraries() const;
    // 读取指定词库的全部字词及拼音（按词库中的原始顺序）
    std::vector<LibraryWordEntry> GetLibraryWords(long long id) const;
    bool MoveMandarinLibrary(long long id, int direction); // direction: -1 上移, +1 下移
    bool DeleteMandarinLibrary(long long id, std::wstring& error);
    bool ImportQcelLibrary(const std::wstring& qcelPath, std::wstring& outName, std::wstring& error);
    std::wstring GetDatabasePath() const { return dbPath_; }

    // 重新加载词库（管理操作后调用）
    void Reload();

private:
    Dictionary() = default;

    std::vector<DictEntry> QueryOnce(const std::string& pinyin, int mode,
                                     const std::string& initialsQuery) const;

    bool LoadFromDatabase(const std::wstring& path);
    void LoadExternal();

    // 词库 schema 迁移与管理辅助
    bool EnsureSchema(struct sqlite3* db);
    bool MigrateLegacyIfNeeded(struct sqlite3* db);
    bool OpenWritableDb(struct sqlite3** outDb, std::wstring& outPath) const;

    std::vector<DictEntry> entries_;
    bool loaded_ = false;
    std::wstring dbPath_;
};

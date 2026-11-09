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
    bool         nasalOnly = false;   // 首音节是鼻音自成音节（n/ng/m/hm/hng，如「嗯」「呒」），没有真正声母
    bool         validPinyin = true;  // 拼音能否完整切分为合法音节（"nihao"=ni+hao 为 true；"n" 为 false）
    bool         viaInitials = false; // 查询结果标记：是否靠声母简拼 / 混拼匹配（词库本身不使用）
    bool         fullWordMatch = false; // 查询结果标记：词条所有音节是否都被输入完整匹配（词库本身不使用）
    int          matchRank = 0;       // 查询结果标记：匹配优先级（越小越靠前，词库本身不使用）
    size_t       matchedLen = 0;      // 查询结果标记：本次实际覆盖的输入字母数（用于更长覆盖优先）
    size_t       consumeInputLen = 0; // 查询结果标记：选中后消耗组合串的字符数（回退候选专用，0 = 按默认规则消耗）
};

// 词典：从外置 SQLite 词库文件加载普通话 + 粤语词条，
// 并根据当前输入模式做候选词优先级排序。
//
// 词库文件位于 DLL 所在目录（安装目录）下的 dicts 子目录：
//   dicts\m2c_dict.db   —— 普通话 + 粤语词条（拼音、声母、音节长度等已预计算）
// 若 dicts 子目录不存在，则回退查找 DLL 所在目录本身。
class Dictionary
{
public:
    static Dictionary& Instance();

    // 加载外置词库（首次访问自动加载）
    void EnsureLoaded();

    // 按拼音（连续串）查找候选词。
    // mode: 0 = 普通话模式（粤语词条排最后），1 = 粤语模式（粤语词条排最前）
    // initialsQuery: 原始组合串，用于声母简拼 / 混拼匹配：
    //   "nh"    -> 你好（全声母简拼）
    //   "sms"   -> 说明书（全声母简拼，zh/ch/sh 可只写首字母）
    //   "nih"   -> 你好（ni 全拼 + h 声母）
    //   "nhao"  -> 你好（n 声母 + hao 全拼）
    //   留空则不启用简拼 / 混拼匹配。
    // 返回按优先级排序后的候选列表。
    //
    // 回退：整串匹配不到任何词组时（如输入 nmzshuoshenme 但词库未收录整句），
    // 按「候选词生成规则」把输入切分为「独立声母 / 完整拼音音节」，
    // 列出所有能完整匹配的词条，并按覆盖的输入字母数由长到短排列
    // （如 农民增收 覆盖 nmzs > 南美洲 / 柠檬汁 覆盖 nmz > 你们 覆盖 nm）。
    // 选中某个回退候选时只消耗它实际匹配到的输入字母，剩余部分继续出候选。
    std::vector<DictEntry> GetCandidates(const std::string& pinyin, int mode,
                                         const std::string& initialsQuery = std::string()) const;

    // 直接添加一条词条（供扩展/测试使用）
    void AddEntry(const std::wstring& word, const std::string& pinyin,
                  int weight, LangSource source);

private:
    Dictionary() = default;

    // 对整串执行一次匹配 + 排序（不含逐级回退），供 GetCandidates 及其回退层调用。
    std::vector<DictEntry> QueryOnce(const std::string& pinyin, int mode,
                                     const std::string& initialsQuery) const;

    // 从 SQLite 词库文件读取词条（表 entries，字段均由导入工具预计算）。
    bool LoadFromDatabase(const std::wstring& path);

    void LoadExternal();

    std::vector<DictEntry> entries_;
    bool loaded_ = false;
};

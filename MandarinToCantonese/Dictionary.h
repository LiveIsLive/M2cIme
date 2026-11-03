#pragma once

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
    std::wstring word;     // 汉字
    std::string  pinyin;   // 连续拼音（小写，无声调，如 "nihao"）
    std::string  initials; // 各音节声母拼接（如 "nihao" -> "nh"）
    int          weight;   // 词频权重（越大越靠前）
    LangSource   source;   // 来源词库
    bool         nasalOnly = false;   // 首音节是鼻音自成音节（n/ng/m/hm/hng，如「嗯」「呒」），没有真正声母
    bool         validPinyin = true;  // 拼音能否完整切分为合法音节（"nihao"=ni+hao 为 true；"n" 为 false）
    bool         viaInitials = false; // 查询结果标记：是否仅靠声母简拼匹配（词库本身不使用）
};

// 词典：从外置 RIME 格式词库文件（.dict.yaml）加载普通话 + 粤语词条，
// 并根据当前输入模式做候选词优先级排序。
//
// 词库文件位于 DLL 所在目录（安装目录）下的 dicts 子目录：
//   dicts\pinyin_simp.dict.yaml        —— 普通话词库（带词频权重）
//   dicts\joy.pinyin_yueyu.dict.yaml   —— 粤语词库（无权重，按文件顺序）
// 若 dicts 子目录不存在，则回退查找 DLL 所在目录本身。
class Dictionary
{
public:
    static Dictionary& Instance();

    // 加载外置词库（首次访问自动加载）
    void EnsureLoaded();

    // 按拼音（连续串）查找候选词。
    // mode: 0 = 普通话模式（粤语词条排最后），1 = 粤语模式（粤语词条排最前）
    // initialsQuery: 原始组合串，用于声母简拼匹配（如 "nh" -> 你好）；留空则不启用。
    // 返回按优先级排序后的候选列表。
    std::vector<DictEntry> GetCandidates(const std::string& pinyin, int mode,
                                         const std::string& initialsQuery = std::string()) const;

    // 直接添加一条词条（供扩展/测试使用）
    void AddEntry(const std::wstring& word, const std::string& pinyin,
                  int weight, LangSource source);

private:
    Dictionary() = default;

    // 从 RIME .dict.yaml 文件加载词条。
    // hasWeight: true 表示每行第 3 列为词频；false 表示按行序生成递减权重。
    bool LoadFromFile(const std::wstring& path, LangSource source, bool hasWeight);

    void LoadExternal();

    std::vector<DictEntry> entries_;
    bool loaded_ = false;
};

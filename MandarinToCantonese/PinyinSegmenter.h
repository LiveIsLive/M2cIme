#pragma once

#include <string>
#include <vector>

// 汉语拼音音节切分器
// 负责把用户输入的连续字母串（如 "nihao"）切分为合法的拼音音节（如 "ni","hao"）
class PinyinSegmenter
{
public:
    // 获取全部合法拼音音节（小写，不含声调）
    static const std::vector<std::string>& GetAllSyllables();

    // 判断一个字符串是否为合法拼音音节
    static bool IsValidSyllable(const std::string& s);

    // 正向最大匹配切分。
    // 若输入包含非小写字母字符（已由调用方过滤）则返回空。
    // 对于无法完全切分的尾部，返回已切分部分并设置 *pRemainder 为剩余串。
    static std::vector<std::string> Segment(const std::string& input,
                                            std::string* pRemainder = nullptr);

    // 把切分后的音节重新拼接为连续串（用于词典匹配）
    static std::string Join(const std::vector<std::string>& syllables);
};

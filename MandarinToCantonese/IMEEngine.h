#pragma once

// 拼音输入法引擎 —— 对外导出的 C 接口
// 支持三种输入模式：英文 / 普通话 / 粤语
// 粤语与普通话模式均基于汉语拼音输入汉字，差异仅在于词库优先级。

// 输入模式
typedef enum ImeInputMode
{
    IME_MODE_ENGLISH   = 0,  // 英文：直接输出 ASCII，不做拼音转换
    IME_MODE_MANDARIN  = 1,  // 普通话：粤语词库强制排到最低
    IME_MODE_CANTONESE = 2,  // 粤语：粤语词库强制排到最高
} ImeInputMode;

// 标点符号模式（中文标点 / 英文标点）
typedef enum ImePunctMode
{
    IME_PUNCT_HALFWIDTH = 0,  // 半角（英文标点）
    IME_PUNCT_FULLWIDTH = 1,  // 全角（中文标点）
} ImePunctMode;

// 字符宽度模式（全角 / 半角）—— 与标点模式相互独立
// 全角时，直接输出的 ASCII 字母、数字、符号、空格会被转换为对应全角字符
typedef enum ImeWidthMode
{
    IME_WIDTH_HALFWIDTH = 0,  // 半角
    IME_WIDTH_FULLWIDTH = 1,  // 全角
} ImeWidthMode;

// 中文字形（简体 / 繁体）—— 只影响汉字候选与上屏文本，不影响拼音输入
typedef enum ImeChineseVariant
{
    IME_VARIANT_SIMPLIFIED  = 0,  // 简体中文
    IME_VARIANT_TRADITIONAL = 1,  // 繁体中文
} ImeChineseVariant;

// 拼音输入方式（全拼 / 双拼）
// 全拼：输入完整拼音字母，如 nihao
// 双拼：每个汉字用「声母键 + 韵母键」两键输入，方案采用微软拼音双拼
typedef enum ImePinyinMode
{
    IME_PINYIN_FULL   = 0,  // 全拼
    IME_PINYIN_DOUBLE = 1,  // 双拼（微软双拼方案）
} ImePinyinMode;

#ifdef __cplusplus
extern "C" {
#endif

// 初始化引擎（加载词库）。首次调用 IME_OnChar 等接口时会自动初始化，
// 也可显式调用以提前加载。
__declspec(dllexport) void IME_Init();

// 切换 / 查询输入模式
__declspec(dllexport) void IME_SetMode(ImeInputMode mode);
__declspec(dllexport) ImeInputMode IME_GetMode();

// 切换 / 查询标点符号模式（中文标点/英文标点）
__declspec(dllexport) void IME_SetPunctMode(ImePunctMode mode);
__declspec(dllexport) ImePunctMode IME_GetPunctMode();

// 切换 / 查询字符宽度模式（全角/半角）
__declspec(dllexport) void IME_SetWidthMode(ImeWidthMode mode);
__declspec(dllexport) ImeWidthMode IME_GetWidthMode();

// 切换 / 查询中文字形（简体/繁体）
// 切换后会立即把当前候选词转换为对应字形；切换输入模式不会重置该状态。
__declspec(dllexport) void IME_SetChineseVariant(ImeChineseVariant variant);
__declspec(dllexport) ImeChineseVariant IME_GetChineseVariant();

// 切换 / 查询拼音输入方式（全拼 / 双拼）
__declspec(dllexport) void IME_SetPinyinMode(ImePinyinMode mode);
__declspec(dllexport) ImePinyinMode IME_GetPinyinMode();

// 按当前宽度模式转换一个 ASCII 字符：全角时返回对应全角字符，否则原样返回
// 空格 → 全角空格(U+3000)，可打印 ASCII(0x21-0x7E) → +0xFEE0
__declspec(dllexport) wchar_t IME_ConvertCharWidth(char ch);

// 在英文模式下，根据当前标点模式转换一个半角标点为全角/半角
// 返回转换后的宽字符（若无需转换则原样返回）
__declspec(dllexport) wchar_t IME_ConvertPunctChar(char ch);

// 处理一个按键字符（a-z / A-Z / 空格 / 数字 / 标点 / 回车）。
// 返回 true 表示该键已被引擎消费（前端不应再默认处理），
// 返回 false 表示引擎未消费（前端按默认行为处理）。
// 注：只输入声母（如 n / zh / b）时同样会产生候选词，列出拼音以该声母开头的词；
// 依次输入各字声母（如 nh / xx）也会列出对应词语（你好 / 谢谢）。
__declspec(dllexport) bool IME_OnChar(char ch);

// 处理退格键。组合串非空时删除最后一个拼音字符并返回 true。
__declspec(dllexport) bool IME_OnBackspace();

// 删除组合串开头的 n 个字符（用于"从头选词"：标点被替换上屏后从组合串里移除）
__declspec(dllexport) bool IME_DeleteCompositionFront(int n);

// 获取当前组合串（正在输入的拼音）。
// buf 为 wchar_t 缓冲区，bufLen 为容量（含结尾 '\0'）。
// 返回写入的字符数（不含 '\0'）。
__declspec(dllexport) int IME_GetComposition(wchar_t* buf, int bufLen);

// 获取候选词数量
__declspec(dllexport) int IME_GetCandidateCount();

// 获取第 index 个候选词（0 基）。返回写入字符数（不含 '\0'）。
__declspec(dllexport) int IME_GetCandidate(int index, wchar_t* buf, int bufLen);

// 获取第 index 个候选词的来源：0=普通话, 1=粤语
__declspec(dllexport) int IME_GetCandidateSource(int index);

// 选择第 index 个候选词并提交。提交后的文本可通过 IME_GetCommitted 读取。
// 支持连续选词：若该候选只覆盖组合串的前半部分拼音（如输入 doubao 时选「豆」），
// 剩余拼音（bao）会保留在组合串中并刷新候选，可继续选词。
// 返回 true 表示成功提交。
__declspec(dllexport) bool IME_SelectCandidate(int index);

// 提交当前组合串中的第一个候选词（等价于按空格）。
__declspec(dllexport) bool IME_CommitFirst();

// 获取最近一次提交的文本。前端读取后应调用 IME_ClearCommitted 清空。
__declspec(dllexport) int IME_GetCommitted(wchar_t* buf, int bufLen);

// 清空已提交文本缓冲
__declspec(dllexport) void IME_ClearCommitted();

// 清空当前组合串与候选（不提交）
__declspec(dllexport) void IME_ClearComposition();

// 引擎是否处于有组合串的输入状态
__declspec(dllexport) bool IME_HasComposition();

#ifdef __cplusplus
}
#endif

#pragma once

// M2CIME —— TSF 系统级输入法（普通话/粤语拼音）
// 全局定义：CLSID / Profile GUID / 语言栏按钮 GUID

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <msctf.h>
#include <ctfutb.h>
#include <string>
#include <vector>

// 文本服务 CLSID
// {7E2A1F30-9C4B-4E1D-B5F2-3A6D8C0E1A42}
DEFINE_GUID(CLSID_M2CTextService,
    0x7e2a1f30, 0x9c4b, 0x4e1d, 0xb5, 0xf2, 0x3a, 0x6d, 0x8c, 0x0e, 0x1a, 0x42);

// 语言 Profile GUID
// {A3F5C8D1-2B6E-4A9F-8D3C-5E7B9A1C2D04}
DEFINE_GUID(GUID_M2CProfile,
    0xa3f5c8d1, 0x2b6e, 0x4a9f, 0x8d, 0x3c, 0x5e, 0x7b, 0x9a, 0x1c, 0x2d, 0x04);

// 语言栏模式切换按钮 GUID
// {B6E8D2A4-1F3C-4B5A-9E7D-8C2A4F6B1D35}
DEFINE_GUID(GUID_M2CLangBarButton,
    0xb6e8d2a4, 0x1f3c, 0x4b5a, 0x9e, 0x7d, 0x8c, 0x2a, 0x4f, 0x6b, 0x1d, 0x35);

// 语言栏中文/英文标点切换按钮 GUID
// {C7F9E3B5-2F4D-5C6B-0E8D-9D3B5A7C2E46}
DEFINE_GUID(GUID_M2CLangBarPunctButton,
    0xc7f9e3b5, 0x2f4d, 0x5c6b, 0x0e, 0x8d, 0x9d, 0x3b, 0x5a, 0x7c, 0x2e, 0x46);

// 语言栏全角/半角切换按钮 GUID
// {D8A0F4C6-305E-6D7C-1F9E-AE4C6B8D3F57}
DEFINE_GUID(GUID_M2CLangBarWidthButton,
    0xd8a0f4c6, 0x305e, 0x6d7c, 0x1f, 0x9e, 0xae, 0x4c, 0x6b, 0x8d, 0x3f, 0x57);

// 语言栏简体/繁体切换按钮 GUID
// {E9B1A5D7-416F-7E8D-2A0F-BF5D7C9E4068}
DEFINE_GUID(GUID_M2CLangBarVariantButton,
    0xe9b1a5d7, 0x416f, 0x7e8d, 0x2a, 0x0f, 0xbf, 0x5d, 0x7c, 0x9e, 0x40, 0x68);

// 语言栏设置按钮 GUID（打开设置对话框）
// {F0C2B6E8-527A-8F9E-3B10-C06E8DA1F179}
DEFINE_GUID(GUID_M2CLangBarSettingsButton,
    0xf0c2b6e8, 0x527a, 0x8f9e, 0x3b, 0x10, 0xc0, 0x6e, 0x8d, 0xa1, 0xf1, 0x79);

// Ctrl+空格 保留键（PreservedKey）命令 GUID：
// 切换中英文模式的主路径是系统"输入法/非输入法切换"热键 → 键盘开/关舱室（见
// TextService.cpp 的 OnChange）。该保留键为备用路径：仅当用户改掉/禁用了系统热键、
// Ctrl+空格 不再被 imm32 层拦截时，组合键才经 ITfKeystrokeMgr::PreserveKey 路由到
// OnPreservedKey 执行切换
// {DED42472-1186-4CCB-8EC1-F9F2567E9891}
DEFINE_GUID(GUID_M2CPreservedKeyCtrlSpace,
    0xded42472, 0x1186, 0x4ccb, 0x8e, 0xc1, 0xf9, 0xf2, 0x56, 0x7e, 0x98, 0x91);

// Ctrl+. 保留键（PreservedKey）命令 GUID：切换中文/英文标点
// {E1D53583-2297-4DDC-9FC2-0A3676F8A9A2}
DEFINE_GUID(GUID_M2CPreservedKeyCtrlPeriod,
    0xe1d53583, 0x2297, 0x4ddc, 0x9f, 0xc2, 0x0a, 0x36, 0x76, 0xf8, 0xa9, 0xa2);

// 显示名称
#define M2C_SERVICE_DESC       L"M2C 拼音（普通话/粤语）"
#define M2C_LANGBAR_DESC       L"M2C 模式切换"
#define M2C_PUNCT_BTN_DESC     L"M2C 中文/英文标点切换"
#define M2C_WIDTH_BTN_DESC     L"M2C 全角/半角切换"
#define M2C_VARIANT_BTN_DESC   L"M2C 简体/繁体切换"
#define M2C_SETTINGS_BTN_DESC  L"M2C 设置"
// 保留键描述（显示在系统"文本服务信息"等热键列表中）
#define M2C_PRESERVEDKEY_CTRLSPACE_DESC  L"M2C 切换中英文模式"
#define M2C_PRESERVEDKEY_CTRLPERIOD_DESC L"M2C 切换中文/英文标点"
// 悬停提示文字（显示当前状态）
#define M2C_TIP_ENGLISH        L"英文(ctrl+空格)"
#define M2C_TIP_MANDARIN       L"普通话(ctrl+空格)"
#define M2C_TIP_CANTONESE      L"粤语(ctrl+空格)"
#define M2C_TIP_CN_PUNCT       L"中文标点(Ctrl+.)"
#define M2C_TIP_EN_PUNCT       L"英文标点(Ctrl+.)"
#define M2C_TIP_FULLWIDTH      L"全角"
#define M2C_TIP_HALFWIDTH      L"半角"
#define M2C_TIP_SIMPLIFIED     L"简体中文"
#define M2C_TIP_TRADITIONAL    L"繁体中文"
#define M2C_TIP_SETTINGS       L"设置"

class CTextService;

// 弹出"设置"对话框（模态）。hwndOwner 可为 nullptr。
// 实现在 LangBarSettingsButton.cpp，供自绘状态条和语言栏按钮共用。
void M2CShowSettingsDialog(CTextService* pService, HWND hwndOwner);

// 每页候选数（数字键 1-9 选词）
#define M2C_PAGE_SIZE 9

// 全局实例与锁计数（dllmain.cpp 中定义）
extern HINSTANCE g_hInst;
extern LONG g_cRefDll;

void DllAddRef();
void DllRelease();

// 引擎头（相对引擎项目目录，由 vcxproj 的 Include 路径解析）
#include "IMEEngine.h"

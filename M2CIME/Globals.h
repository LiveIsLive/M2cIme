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

// 显示名称
#define M2C_SERVICE_DESC   L"M2C 拼音（普通话/粤语）"
#define M2C_LANGBAR_DESC   L"M2C 模式切换"
#define M2C_LANGBAR_TIP    L"点击切换：英文 / 普通话 / 粤语"

// 每页候选数（数字键 1-9 选词）
#define M2C_PAGE_SIZE 9

// 全局实例与锁计数（dllmain.cpp 中定义）
extern HINSTANCE g_hInst;
extern LONG g_cRefDll;

void DllAddRef();
void DllRelease();

// 引擎头（相对引擎项目目录，由 vcxproj 的 Include 路径解析）
#include "IMEEngine.h"

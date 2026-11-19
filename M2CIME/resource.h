#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDI_M2C 101

// Settings dialog (keep comments ASCII: this header is also parsed by rc.exe,
// where a non-ASCII comment can swallow the following #define under the wrong code page)
#define IDD_SETTINGS            201
#define IDD_DICT_MGR            202
#define IDD_DICT_VIEW           203

#define IDC_INPUT_MODE_COMBO    1001
#define IDC_MANAGE_DICTS_BTN    1002

// Dictionary manager dialog
#define IDC_DICT_LIST           1101
#define IDC_DICT_MOVE_UP        1102
#define IDC_DICT_MOVE_DOWN      1103
#define IDC_DICT_IMPORT         1104
#define IDC_DICT_DELETE         1105
#define IDC_DICT_DOWNLOAD       1106
#define IDC_DICT_VIEW           1107

// Dictionary viewer dialog
#define IDC_DICT_VIEW_INFO      1201
#define IDC_DICT_VIEW_LIST      1202

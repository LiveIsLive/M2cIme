; M2C 拼音输入法 —— Inno Setup 安装包脚本
; 构建命令：ISCC.exe setup.iss

#define AppName "M2C 拼音输入法"
#define AppVersion "1.0.0"
#define AppPublisher "M2C"

[Setup]
AppId={{3F8A1C2E-7B4D-4E5F-9A1B-2C3D4E5F6A77}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\M2CIME
DefaultGroupName={#AppName}
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=installer
OutputBaseFilename=M2CIME_Setup_{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\M2CIME.dll

[Files]
Source: "M2CIME\x64\Release\M2CIME.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "M2CIME\Release\M2CIME32.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace
Source: "M2CIME\M2CIME.ico"; DestDir: "{app}"; Flags: ignoreversion
; 外置词库（SQLite 数据库，可用 sqlite3 工具维护，改完重新部署生效）
Source: "dicts\m2c_dict.db"; DestDir: "{app}\dicts"; Flags: ignoreversion

[Run]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im ctfmon.exe"; Flags: runhidden waituntilterminated skipifdoesntexist; StatusMsg: "正在关闭语言栏进程..."
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\M2CIME.dll"""; Flags: runhidden waituntilterminated; StatusMsg: "正在注册输入法（64 位）..."
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s ""{app}\M2CIME32.dll"""; Flags: runhidden waituntilterminated; StatusMsg: "正在注册输入法（32 位）..."
Filename: "{sys}\ctfmon.exe"; Flags: runhidden nowait skipifdoesntexist

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im ctfmon.exe"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "KillCtfmon"
Filename: "{sys}\regsvr32.exe"; Parameters: "/s /u ""{app}\M2CIME.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "Unreg64"
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s /u ""{app}\M2CIME32.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "Unreg32"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
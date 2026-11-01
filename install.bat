@echo off
chcp 65001 >nul
:: M2C 拼音输入法 —— 开发版安装（原地注册编译输出）
:: 需要管理员权限
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 请右键以管理员身份运行本脚本。
    pause
    exit /b 1
)

set DIR=%~dp0

echo 正在注册 64 位输入法组件...
regsvr32 /s "%DIR%M2CIME\x64\Release\M2CIME.dll"
if %errorlevel% neq 0 (
    echo [错误] 64 位注册失败。
    pause
    exit /b 1
)

echo 正在注册 32 位输入法组件（WOW64）...
"%SystemRoot%\SysWOW64\regsvr32.exe" /s "%DIR%M2CIME\Release\M2CIME32.dll"
if %errorlevel% neq 0 (
    echo [错误] 32 位注册失败。
    pause
    exit /b 1
)

echo 正在重启 ctfmon 使语言栏刷新...
taskkill /f /im ctfmon.exe >nul 2>&1
start "" "%SystemRoot%\System32\ctfmon.exe"

echo.
echo 安装完成！请在任务栏语言栏中切换到 "M2C 拼音（普通话/粤语）"。
echo 若语言栏未出现，请注销并重新登录 Windows。
pause

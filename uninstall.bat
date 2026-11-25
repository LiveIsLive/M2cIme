@echo off
chcp 936 >nul
:: M2C 拼音输入法 —— 开发版卸载
:: 用 fsutil 检查管理员权限（不依赖 Server 服务，net session 在服务禁用时会误报）
fsutil dirty query %SystemDrive% >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 请右键以管理员身份运行本脚本。
    pause
    exit /b 1
)

set DIR=%~dp0

regsvr32 /s /u "%DIR%x64\Release\M2CIME.dll"
"%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%DIR%Release\M2CIME32.dll"

taskkill /f /im ctfmon.exe >nul 2>&1
start "" "%SystemRoot%\System32\ctfmon.exe"

echo 卸载完成。
pause

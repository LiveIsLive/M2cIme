@echo off
chcp 65001 >nul
:: M2C 拼音输入法 —— 开发版卸载
net session >nul 2>&1
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

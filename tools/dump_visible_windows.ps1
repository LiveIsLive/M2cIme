# 列出当前桌面上所有可见的顶层窗口，用于排查“多出一条语言栏”时窗口属于哪个进程。
# 用法：在语言栏重复出现时运行  powershell -ExecutionPolicy Bypass -File tools\dump_visible_windows.ps1
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class VisibleWindowDumper
{
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$rows = New-Object System.Collections.ArrayList
$callback = [VisibleWindowDumper+EnumWindowsProc]{
    param($hWnd, $lParam)
    if ([VisibleWindowDumper]::IsWindowVisible($hWnd))
    {
        $cls = New-Object System.Text.StringBuilder 256
        [void][VisibleWindowDumper]::GetClassName($hWnd, $cls, $cls.Capacity)
        $title = New-Object System.Text.StringBuilder 256
        [void][VisibleWindowDumper]::GetWindowText($hWnd, $title, $title.Capacity)
        $procId = 0
        [void][VisibleWindowDumper]::GetWindowThreadProcessId($hWnd, [ref]$procId)
        $rect = New-Object VisibleWindowDumper+RECT
        [void][VisibleWindowDumper]::GetWindowRect($hWnd, [ref]$rect)
        $procName = try { (Get-Process -Id $procId -ErrorAction Stop).ProcessName } catch { '?' }
        [void]$rows.Add([pscustomobject]@{
            HWND  = $hWnd
            PID   = $procId
            Process = $procName
            Class = $cls.ToString()
            Title = $title.ToString()
            Rect  = "$($rect.Left),$($rect.Top) - $($rect.Right),$($rect.Bottom)"
        })
    }
    return $true
}
[void][VisibleWindowDumper]::EnumWindows($callback, [IntPtr]::Zero)
$rows | Sort-Object Process, Class | Format-Table -AutoSize

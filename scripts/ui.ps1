# UI automation for mk_emul (used to check the UI without a person): window screenshot and clicks.
# Usage: ui.ps1 start | stop | shot -Out file.png | click -X 268 -Y 46  (window-relative pixels)
param(
    [string]$Action = "shot",       # shot | click | start | stop
    [int]$X = 0, [int]$Y = 0,       # click position relative to the window's top-left
    [string]$Out = "shot.png"
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, int flags);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, int flags, int extra);
}
"@
[W]::SetProcessDPIAware() | Out-Null
$exe = "C:\mk_emul\build\mk_emul.exe"

function Focus-Win($h) {
    if ([W]::IsIconic($h)) { [W]::ShowWindow($h, 9) | Out-Null }
    [W]::ShowWindow($h, 3) | Out-Null
    # A synthetic ALT press lets SetForegroundWindow succeed from a background process.
    [W]::keybd_event(0x12, 0, 0, 0); [W]::keybd_event(0x12, 0, 2, 0)
    [W]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 300
}

function Get-Win {
    $p = Get-Process mk_emul -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { throw "mk_emul window not found" }
    return $p.MainWindowHandle
}

switch ($Action) {
    "start" {
        Get-Process mk_emul -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Process $exe -WorkingDirectory (Split-Path $exe)
        for ($i = 0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 250; try { $h = Get-Win; break } catch {} }
        [W]::ShowWindow($h, 3) | Out-Null   # maximize for a stable layout
        Start-Sleep -Milliseconds 800
        "started"
    }
    "stop" { Get-Process mk_emul -ErrorAction SilentlyContinue | Stop-Process -Force; "stopped" }
    "click" {
        $h = Get-Win; Focus-Win $h
        $r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
        [W]::SetCursorPos($r.L + $X, $r.T + $Y) | Out-Null
        Start-Sleep -Milliseconds 100
        [W]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 60; [W]::mouse_event(4, 0, 0, 0, 0)
        "clicked $X,$Y"
    }
    "shot" {
        $h = Get-Win
        if ([W]::IsIconic($h)) { Focus-Win $h }
        $r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $hh = $r.B - $r.T
        $bmp = New-Object System.Drawing.Bitmap $w, $hh
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc(); [W]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
        $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
        "saved $Out ${w}x${hh} at $($r.L),$($r.T)"
    }
}

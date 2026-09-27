# UI automation for mk_emul (used to check the UI without a person): window screenshot and clicks.
# Usage: ui.ps1 start [-X -1928 -Y 410] | stop | shot -Out file.png | click -X 268 -Y 46 [-Double] | keys -Text "^a0x2000{ENTER}"
#        (click coords are window-relative pixels; keys uses SendKeys syntax: ^=Ctrl, {ENTER}, {TAB})
param(
    [string]$Action = "shot",       # shot | screen | click | keys | info | start | stop | uia-list | uia-set | uia-press
    [int]$X = 0, [int]$Y = 0,       # click position relative to the window's top-left
    [string]$Out = "shot.png",
    [string]$Text = "",
    [string]$Name = "",
    [switch]$Double,
    [int]$Count = 1,                # shot: number of frames (Out gets _1, _2, ... suffixes)
    [int]$IntervalMs = 100
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
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, int flags, int extra);
}
"@
[W]::SetProcessDPIAware() | Out-Null
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$exe = "C:\mk_emul\build\mk_emul.exe"

function Focus-Win($h) {
    if ([W]::IsIconic($h)) { [W]::ShowWindow($h, 9) | Out-Null }   # restore, keep size/monitor
    if ([W]::GetForegroundWindow() -eq $h) { return }
    # A synthetic ALT press lets SetForegroundWindow succeed from a background process.
    # Only when needed: Qt swallows the key typed right after an ALT tap.
    [W]::keybd_event(0x12, 0, 0, 0); [W]::keybd_event(0x12, 0, 2, 0)
    [W]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 300
}

function Get-Win {
    $p = Get-Process mk_emul -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { throw "mk_emul window not found" }
    return $p.MainWindowHandle
}

# Top-level window for UI Automation: the app's dialog if one is open, else the main window.
function Get-UiaRoot {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
    $procId = (Get-Process mk_emul | Select-Object -First 1).Id
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $procId)
    $wins = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
    $main = Get-Win
    foreach ($w in $wins) { if ($w.Current.NativeWindowHandle -ne [int]$main) { return $w } }
    foreach ($w in $wins) {
        # Dialogs owned by the main window may be its UIA children.
        $d = $w.FindFirst([System.Windows.Automation.TreeScope]::Children,
            (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Window)))
        if ($d) { return $d }
        return $w
    }
}

function Find-Uia($root, $type, $name) {
    $all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    if ($name -ne "") {
        # Match by name or automation id regardless of type (Win32 dialog controls show up as Pane).
        # Prefer real Win32 controls: list items can reuse ids like "1".
        foreach ($native in @($true, $false)) {
            foreach ($e in $all) {
                if ($native -and $e.Current.NativeWindowHandle -eq 0) { continue }
                if ($e.Current.Name -eq $name -or $e.Current.AutomationId -eq $name) { return $e }
            }
        }
    } else {
        foreach ($e in $all) { if ($e.Current.ControlType -eq $type) { return $e } }
    }
    throw "UIA element not found: $($type.ProgrammaticName) '$name'"
}

switch ($Action) {
    "uia-list" {
        $root = Get-UiaRoot
        "root: $($root.Current.Name)"
        $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
            ForEach-Object { "$($_.Current.ControlType.ProgrammaticName) | id=$($_.Current.AutomationId) | $($_.Current.Name)" } | Select-Object -First 200
    }
    "uia-set" {
        $root = Get-UiaRoot
        $e = Find-Uia $root ([System.Windows.Automation.ControlType]::Edit) $Name
        $hw = [IntPtr]$e.Current.NativeWindowHandle
        if ($hw -ne [IntPtr]::Zero) { [W]::SendMessage($hw, 0x000C, [IntPtr]::Zero, $Text) | Out-Null }   # WM_SETTEXT
        else { $e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($Text) }
        "set '$($e.Current.Name)' in '$($root.Current.Name)'"
    }
    "uia-press" {
        $root = Get-UiaRoot
        $e = Find-Uia $root ([System.Windows.Automation.ControlType]::Button) $Name
        $hw = [IntPtr]$e.Current.NativeWindowHandle
        if ($hw -ne [IntPtr]::Zero) { [W]::SendMessage($hw, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }  # BM_CLICK
        else { $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
        "pressed '$Name' in '$($root.Current.Name)'"
    }
    "start" {
        Get-Process mk_emul -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Process $exe -WorkingDirectory (Split-Path $exe)
        for ($i = 0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 250; try { $h = Get-Win; break } catch {} }
        # -X/-Y given: place the window there (e.g. on a second monitor) with a fixed size,
        # otherwise maximize on the current monitor.
        if ($X -ne 0 -or $Y -ne 0) { [W]::MoveWindow($h, $X, $Y, 1936, 1048, $true) | Out-Null }
        else { [W]::ShowWindow($h, 3) | Out-Null }
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
        if ($Double) { Start-Sleep -Milliseconds 80; [W]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 60; [W]::mouse_event(4, 0, 0, 0, 0) }
        "clicked $X,$Y"
    }
    "keys" {
        # Sends keystrokes to mk_emul only: its open dialog if there is one, else the main window.
        # Refuses to type if focus cannot be moved there (never types into another app).
        Add-Type -AssemblyName System.Windows.Forms
        $procId = (Get-Process mk_emul | Select-Object -First 1).Id
        $main = Get-Win
        $script:target = $main
        [W]::EnumWindows({ param($hw, $p)
            $wp = 0; [W]::GetWindowThreadProcessId($hw, [ref]$wp) | Out-Null
            if ($wp -eq $procId -and $hw -ne $main -and [W]::IsWindowVisible($hw) -and [W]::IsWindowEnabled($hw)) {
                $sb = New-Object System.Text.StringBuilder 256; [W]::GetWindowText($hw, $sb, 256) | Out-Null
                if ($sb.Length -gt 0) { $script:target = $hw; return $false }
            }
            return $true }, [IntPtr]::Zero) | Out-Null
        if ([W]::GetForegroundWindow() -ne $script:target) {
            [W]::keybd_event(0x12, 0, 0, 0); [W]::keybd_event(0x12, 0, 2, 0)
            [W]::SetForegroundWindow($script:target) | Out-Null
            Start-Sleep -Milliseconds 300
        }
        $fg = [W]::GetForegroundWindow(); $fp = 0; [W]::GetWindowThreadProcessId($fg, [ref]$fp) | Out-Null
        if ($fp -ne $procId) { throw "focus is not on mk_emul (pid $fp) - keys NOT sent" }
        [System.Windows.Forms.SendKeys]::SendWait($Text)
        "sent keys to window $($script:target)"
    }
    "info" {
        Get-Process mk_emul -ErrorAction SilentlyContinue | ForEach-Object {
            $r = New-Object W+RECT; [W]::GetWindowRect($_.MainWindowHandle, [ref]$r) | Out-Null
            "pid=$($_.Id) title='$($_.MainWindowTitle)' rect=$($r.L),$($r.T),$($r.R),$($r.B) iconic=$([W]::IsIconic($_.MainWindowHandle))"
        }
    }
    "screen" {
        # Screen pixels over the main window's rectangle (what the user sees, incl. dialogs).
        $h = Get-Win
        $r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $hh = $r.B - $r.T
        $sw = [Diagnostics.Stopwatch]::StartNew()
        for ($i = 1; $i -le $Count; $i++) {
            $bmp = New-Object System.Drawing.Bitmap $w, $hh
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
            $file = if ($Count -gt 1) { $Out -replace '\.png$', "_$i.png" } else { $Out }
            $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
            "saved $file at $($sw.ElapsedMilliseconds) ms"
            if ($i -lt $Count) { $next = $i * $IntervalMs; while ($sw.ElapsedMilliseconds -lt $next) { Start-Sleep -Milliseconds 5 } }
        }
    }
    "shot" {
        $h = Get-Win
        if ([W]::IsIconic($h)) { Focus-Win $h }
        $r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $hh = $r.B - $r.T
        $sw = [Diagnostics.Stopwatch]::StartNew()
        for ($i = 1; $i -le $Count; $i++) {
            $bmp = New-Object System.Drawing.Bitmap $w, $hh
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $hdc = $g.GetHdc(); [W]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
            $file = if ($Count -gt 1) { $Out -replace '\.png$', "_$i.png" } else { $Out }
            $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
            "saved $file at $($sw.ElapsedMilliseconds) ms"
            if ($i -lt $Count) { $next = $i * $IntervalMs; while ($sw.ElapsedMilliseconds -lt $next) { Start-Sleep -Milliseconds 5 } }
        }
    }
}

<#
.SYNOPSIS
    Shared helpers for the developer test scripts. Dot-source it: . "$PSScriptRoot\UiHelpers.ps1"

.DESCRIPTION
    Finds the built tools, drives the mouse and keyboard, finds windows (by class and owning
    process), takes captures, and starts a test copy of EdgeDeck that cannot touch the real
    one: it runs in-process in `apptest.exe` with LOCALAPPDATA pointed at a scratch folder, so
    it reads and writes its own config and log.

    These helpers move the real cursor and press real keys. Read tools/README.md first.
#>

if (-not ('Ui' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class Ui {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attribute, out RECT r, int size);
    [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint action, uint param, out RECT r, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public static List<string> Find(string cls) {
        var res = new List<string>();
        EnumWindows((h, l) => {
            var sb = new StringBuilder(128);
            GetClassName(h, sb, 128);
            if (sb.ToString() == cls) {
                RECT r; GetWindowRect(h, out r);
                uint pid; GetWindowThreadProcessId(h, out pid);
                res.Add(h.ToInt64() + "," + r.L + "," + r.T + "," + r.R + "," + r.B + "," +
                        (IsWindowVisible(h) ? 1 : 0) + "," + GetDpiForWindow(h) + "," + pid);
            }
            return true;
        }, IntPtr.Zero);
        return res;
    }
}
"@
}
# Per-monitor v2, so every coordinate below is a physical pixel and matches what the app sees.
[Ui]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$script:RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# --- the built tools -----------------------------------------------------------------------

function Get-ToolsDir {
    <# Where the built tools are. EDGEDECK_TOOLS_DIR wins; otherwise build-tools\ (the folder
       the README suggests), then build\ for anyone who turned the option on in the main build. #>
    param([string]$Override)
    $candidates = @($Override, $env:EDGEDECK_TOOLS_DIR,
                    (Join-Path $script:RepoRoot 'build-tools\tools\Release'),
                    (Join-Path $script:RepoRoot 'build\tools\Release')) | Where-Object { $_ }
    foreach ($dir in $candidates) { if (Test-Path -LiteralPath (Join-Path $dir 'apptest.exe')) { return $dir } }
    throw "The developer tools are not built. Run:`n  cmake -S . -B build-tools -G `"Visual Studio 17 2022`" -A x64 -DEDGEDECK_BUILD_TOOLS=ON -DEDGEDECK_BUILD_TESTS=OFF`n  cmake --build build-tools --config Release"
}

function Get-Tool {
    param([Parameter(Mandatory)][string]$Name, [string]$ToolsDir)
    $path = Join-Path (Get-ToolsDir $ToolsDir) "$Name.exe"
    if (-not (Test-Path -LiteralPath $path)) { throw "$Name.exe was not found next to the other tools." }
    return $path
}

# --- windows, mouse, keyboard, captures -----------------------------------------------------

function Get-Wins {
    <# Windows of a class, optionally only those of one process. #>
    param([Parameter(Mandatory)][string]$Class, [int]$ProcessId = 0)
    [Ui]::Find($Class) | ForEach-Object {
        $p = $_.Split(',')
        [pscustomobject]@{ Hwnd = [int64]$p[0]; L = [int]$p[1]; T = [int]$p[2]; R = [int]$p[3]; B = [int]$p[4]
                           Visible = ($p[5] -eq '1'); Dpi = [int]$p[6]; Pid = [int]$p[7] }
    } | Where-Object { $ProcessId -eq 0 -or $_.Pid -eq $ProcessId }
}

function Get-PrimaryRect {
    <# The primary monitor, which is always at the origin, in physical pixels. #>
    [pscustomobject]@{ L = 0; T = 0; R = [Ui]::GetSystemMetrics(0); B = [Ui]::GetSystemMetrics(1) }
}

function Get-WorkArea {
    <# The primary monitor minus the taskbar: the size a normal maximized window gets. #>
    $r = New-Object Ui+RECT
    [void][Ui]::SystemParametersInfo(0x0030, 0, [ref]$r, 0)
    [pscustomobject]@{ L = $r.L; T = $r.T; R = $r.R; B = $r.B }
}

function Get-VisibleRect {
    <# A top-level window's visible bounds. Its window rectangle also includes an invisible
       resize border of a few pixels whose picture is whatever is behind it, so a capture of
       an ordinary window should use this, not Get-Wins' rectangle. #>
    param([Parameter(Mandatory)][int64]$Hwnd)
    $r = New-Object Ui+RECT
    if ([Ui]::DwmGetWindowAttribute([IntPtr]$Hwnd, 9, [ref]$r, 16) -ne 0) {
        [void][Ui]::GetWindowRect([IntPtr]$Hwnd, [ref]$r)
    }
    [pscustomobject]@{ L = $r.L; T = $r.T; R = $r.R; B = $r.B; Width = $r.R - $r.L; Height = $r.B - $r.T }
}

function Move-Mouse { param([double]$X, [double]$Y) [void][Ui]::SetCursorPos([int]$X, [int]$Y) }

function Click-Mouse {
    [Ui]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [Ui]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

function Press-Chord {
    <# Ctrl+Alt+Shift+<key>, the way a global hotkey expects it. $Key is a virtual-key code. #>
    param([Parameter(Mandatory)][byte]$Key)
    foreach ($m in 0x11, 0x12, 0x10) { [Ui]::keybd_event($m, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 30 }
    [Ui]::keybd_event($Key, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40
    [Ui]::keybd_event($Key, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 30
    foreach ($m in 0x10, 0x12, 0x11) { [Ui]::keybd_event($m, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 30 }
}

function Take-Shot {
    <# Captures a screen rectangle. Whatever is on top of it is in the picture, so only ever
       point it at a window that is on top of a plain backdrop (see Start-Backdrop). #>
    param([int]$X, [int]$Y, [int]$Width, [int]$Height, [Parameter(Mandatory)][string]$Path)
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($X, $Y, 0, 0, (New-Object System.Drawing.Size($Width, $Height)))
    $g.Dispose(); $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

function Wait-Until {
    param([Parameter(Mandatory)][scriptblock]$Condition, [double]$TimeoutSeconds = 20, [int]$PollMs = 300)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) { if (& $Condition) { return $true }; Start-Sleep -Milliseconds $PollMs }
    return $false
}

# --- a test copy of EdgeDeck ----------------------------------------------------------------

function New-ScratchDir {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ("edgedeck-tools-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

function Assert-NoRealInstance {
    <# A second copy cannot run beside the real one: it holds the single-instance lock and the
       global hotkeys. apptest has no lock, but a script that exercises hotkeys needs them free. #>
    param([switch]$StopIt)
    $running = @(Get-Process -Name EdgeDeck -ErrorAction SilentlyContinue)
    if ($running.Count -eq 0) { return }
    if (-not $StopIt) {
        throw "EdgeDeck is running (pid $($running.Id -join ', ')). Close it, or pass -StopRunningInstance; scripts that use hotkeys or measure resources cannot run beside it. scripts\install.ps1 starts it again."
    }
    $running | Stop-Process -Force
    Start-Sleep -Milliseconds 800
}

function Start-TestApp {
    <#
    .SYNOPSIS  Starts apptest.exe (the real App in-process) against a scratch config.
    .PARAMETER Tabs      Widget names: QuickActions, Media, Volume, Brightness, Lyrics, Displays.
    .PARAMETER Settings  Lines written under [settings], e.g. @('theme=Dark','fullscreenGuard=0').
    .OUTPUTS   An object with Process, Scratch, AppData, Trigger and Log.
    #>
    param([string[]]$Tabs = @('QuickActions'), [string[]]$Settings = @('theme=Dark'), [string]$ToolsDir,
          [double]$StartupSeconds = 3)
    $scratch = New-ScratchDir
    $appdata = Join-Path $scratch 'appdata'
    $trigger = Join-Path $scratch 'trig'
    New-Item -ItemType Directory -Path "$appdata\EdgeDeck", $trigger -Force | Out-Null

    $text = "[settings]`n" + (($Settings | ForEach-Object { $_ + "`n" }) -join '')
    for ($i = 0; $i -lt $Tabs.Count; $i++) {
        $ratio = if ($Tabs.Count -eq 1) { 0.5 } else { 0.1 + 0.8 * $i / ($Tabs.Count - 1) }
        $text += "[tab]`nwidget=$($Tabs[$i])`nverticalRatio=$($ratio.ToString([Globalization.CultureInfo]::InvariantCulture))`n" +
                 "tabWidth=26`ntabHeight=76`npanelWidth=320`n"
    }
    Set-Content -Path "$appdata\EdgeDeck\config.txt" -Value $text -Encoding ASCII

    $real = $env:LOCALAPPDATA
    $env:LOCALAPPDATA = $appdata
    try { $process = Start-Process -FilePath (Get-Tool apptest $ToolsDir) -ArgumentList "`"$trigger`"" -PassThru }
    finally { $env:LOCALAPPDATA = $real }
    Start-Sleep -Seconds $StartupSeconds
    [pscustomobject]@{ Process = $process; Scratch = $scratch; AppData = $appdata; Trigger = $trigger
                       Log = "$appdata\EdgeDeck\edgedeck.log" }
}

function Get-TestTabs {
    <# The test copy's own tab windows, top to bottom - never the real EdgeDeck's, which is on the same screen. #>
    param([Parameter(Mandatory)]$App)
    @(Get-Wins 'EdgeDeckTabWindow' -ProcessId $App.Process.Id | Where-Object Visible | Sort-Object T)
}

function Get-TestPanel {
    param([Parameter(Mandatory)]$App)
    @(Get-Wins 'EdgeDeckPanelWindow' -ProcessId $App.Process.Id | Where-Object Visible)[0]
}

function Send-TestApp {
    <# Posts a problem report into the test copy's first tab, exactly as a widget would. #>
    param([Parameter(Mandatory)]$App, [Parameter(Mandatory)][string]$Text)
    Set-Content -Path (Join-Path $App.Trigger ("trig_" + [Guid]::NewGuid().ToString('N') + '.txt')) -Value $Text -Encoding UTF8
}

function Stop-TestApp {
    <# Asks the test copy to exit, falls back to killing it, and removes its scratch folder. #>
    param([Parameter(Mandatory)]$App, [switch]$KeepScratch)
    Set-Content -Path (Join-Path $App.Trigger 'quit.txt') -Value 'x'
    if (-not $App.Process.WaitForExit(8000)) { Stop-Process -Id $App.Process.Id -Force -ErrorAction SilentlyContinue }
    if (-not $KeepScratch) { Remove-Item $App.Scratch -Recurse -Force -ErrorAction SilentlyContinue }
}

function Start-Backdrop {
    <# A plain topmost window, so a capture of something on top of it holds nothing else. #>
    param([int]$X, [int]$Y, [int]$Width, [int]$Height, [int]$Seconds = 60, [string]$Color = '2B2B2B', [string]$ToolsDir)
    $p = Start-Process -FilePath (Get-Tool backdrop $ToolsDir) -ArgumentList "$X $Y $Width $Height $Seconds $Color" -PassThru
    Start-Sleep -Milliseconds 700
    return $p
}

# --- which monitors are on ------------------------------------------------------------------

function Get-DisplayState {
    <# Parses `dispcfg list`: for each of main / tv / all, whether it can be applied with what is
       plugged in and whether it is what is on right now. #>
    param([string]$ToolsDir)
    $state = @{}
    foreach ($line in (& (Get-Tool dispcfg $ToolsDir) list)) {
        if ($line -match 'profile (\w+)\s+available=(\d)\s+active=(\d)') {
            $state[$Matches[1]] = [pscustomobject]@{ Available = ($Matches[2] -eq '1'); Active = ($Matches[3] -eq '1') }
        }
    }
    return $state
}

function Wait-DisplayProfile {
    param([Parameter(Mandatory)][ValidateSet('main', 'tv', 'all')][string]$Profile, [double]$TimeoutSeconds = 25, [string]$ToolsDir)
    Wait-Until -TimeoutSeconds $TimeoutSeconds -Condition { (Get-DisplayState $ToolsDir)[$Profile].Active }
}

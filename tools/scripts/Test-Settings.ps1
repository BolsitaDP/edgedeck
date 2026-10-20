<#
.SYNOPSIS
    Drives the Settings and Shortcuts windows of a test copy the way a person would, and checks
    what each change does to the app, to config.txt and to the system-wide shortcuts.

.DESCRIPTION
    Runs test copies of EdgeDeck (apptest.exe, scratch config) and opens Settings in them through
    their trigger folder. Combo boxes and buttons are set with the messages a click sends; the
    hotkey boxes are typed into with the real keyboard, since that is the only way they take keys.

      1. The Theme box shows the theme that is set (it once always showed "Follow Windows", and
         Save then threw an "Always dark" choice away).
      2. Español + Save: config.txt says language=es and the panel is drawn in Spanish (a picture
         is kept with -ShotDir).
      3. Shortcuts: Backspace leaves a box with no shortcut and frees its keys; a new chord is
         registered and the old one released; a chord another program holds is said in the
         window, which stays open; "Off" for a family frees its keys.

    Whether a chord is taken is asked of Windows itself: this script tries to register it.

    Moves the cursor, clicks in the test copy's windows and presses keys - only while a test copy's
    window is in front. Safe beside the real EdgeDeck, except that its shortcuts would be taken:
    close it first.
#>
[CmdletBinding()]
param([string]$ToolsDir, [string]$ShotDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"
Add-Type -AssemblyName System.Drawing

if (-not ('Dlg' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Dlg {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr h, int id, uint mods, uint vk);
    [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr h, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    public static string Text(IntPtr h) { var sb = new StringBuilder(1024); GetWindowText(h, sb, 1024); return sb.ToString(); }
    // The texts of every STATIC child, joined: where a window's status line can be read.
    public static string Statics(IntPtr dialog) {
        var all = new StringBuilder();
        for (IntPtr c = GetWindow(dialog, 5); c != IntPtr.Zero; c = GetWindow(c, 2)) {
            var cls = new StringBuilder(32); GetClassName(c, cls, 32);
            if (cls.ToString() == "Static") all.Append(Text(c)).Append(" | ");
        }
        return all.ToString();
    }
}
"@
}

$failures = 0
function Check($label, $ok, $detail = '') {
    '{0} {1}{2}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $(if (-not $ok -and $detail) { "  [$detail]" } else { '' })
    if (-not $ok) { $script:failures++ }
}

$CB_GETCURSEL = 0x0147; $CB_SETCURSEL = 0x014E; $BM_CLICK = 0x00F5; $WM_COMMAND = 0x0111; $CBN_SELCHANGE = 1
$MOD_ALT = 1; $MOD_CONTROL = 2
$prim = Get-PrimaryRect

function Find-Window($app, $class) {
    $w = @(Get-Wins $class -ProcessId $app.Process.Id | Where-Object Visible)[0]
    if ($w) { [IntPtr][long]$w.Hwnd } else { [IntPtr]::Zero }
}
function Open-Settings($app) {
    Set-Content -Path (Join-Path $app.Trigger 'settings.txt') -Value 'x'
    [void](Wait-Until -TimeoutSeconds 5 -PollMs 100 -Condition { (Find-Window $app 'EdgeDeckSettingsWindow') -ne [IntPtr]::Zero })
    Start-Sleep -Milliseconds 300
    Find-Window $app 'EdgeDeckSettingsWindow'
}
function Select-Combo($dialog, $id, $index) {
    $combo = [Dlg]::GetDlgItem($dialog, $id)
    [void][Dlg]::SendMessage($combo, $CB_SETCURSEL, [IntPtr]$index, [IntPtr]::Zero)
    [void][Dlg]::SendMessage($dialog, $WM_COMMAND, [IntPtr](($CBN_SELCHANGE -shl 16) -bor $id), $combo)
}
function Click-Button($dialog, $id) { [void][Dlg]::SendMessage([Dlg]::GetDlgItem($dialog, $id), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) }
function Read-Config($app) { Get-Content (Join-Path $app.AppData 'EdgeDeck\config.txt') -Raw -Encoding UTF8 }

# Whether Windows would give these keys to anyone else right now: try to take them, and let go.
function Test-ChordFree([uint32]$mods, [uint32]$vk) {
    $ok = [Dlg]::RegisterHotKey([IntPtr]::Zero, 0x6E01, $mods, $vk)
    if ($ok) { [void][Dlg]::UnregisterHotKey([IntPtr]::Zero, 0x6E01) }
    return $ok
}

# Puts a test copy's window in front and the keyboard in one of its controls: Windows lets a
# process bring a window forward just after a key press, and a click gives the box the focus.
function Focus-Control($dialog, $id) {
    [Ui]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [Ui]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [void][Dlg]::SetForegroundWindow($dialog)
    Start-Sleep -Milliseconds 200
    $box = New-Object Dlg+Rect
    [void][Dlg]::GetWindowRect([Dlg]::GetDlgItem($dialog, $id), [ref]$box)
    Move-Mouse (($box.L + $box.R) / 2) (($box.T + $box.B) / 2); Start-Sleep -Milliseconds 100
    Click-Mouse; Start-Sleep -Milliseconds 200
}
function Press-Keys([byte[]]$keys) {
    foreach ($k in $keys) { [Ui]::keybd_event($k, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40 }
    [array]::Reverse($keys)
    foreach ($k in $keys) { [Ui]::keybd_event($k, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40 }
    Start-Sleep -Milliseconds 200
}

# ---- 1. the Theme box shows what is set -------------------------------------------------------
foreach ($case in @(@{ Theme = 'Dark'; Index = 1 }, @{ Theme = 'Light'; Index = 2 }, @{ Theme = 'Follow'; Index = 0 })) {
    $app = Start-TestApp -Tabs QuickActions -Settings @("theme=$($case.Theme)", 'fullscreenGuard=0')
    try {
        $dialog = Open-Settings $app
        $shown = [int][Dlg]::SendMessage([Dlg]::GetDlgItem($dialog, 113), $CB_GETCURSEL, [IntPtr]::Zero, [IntPtr]::Zero)
        Check "1. theme=$($case.Theme): the Theme box shows it" ($shown -eq $case.Index) "index $shown"
        Click-Button $dialog 109 # Save, untouched
        Start-Sleep -Milliseconds 500
        Check "   and saving untouched keeps it" ((Read-Config $app) -match "(?m)^theme=$($case.Theme)\r?$") ((Read-Config $app) -split "`n" | Select-String '^theme')
    } finally { Stop-TestApp $app }
}

# ---- 2. the language, chosen and saved --------------------------------------------------------
# The plain window the panel is pictured against comes first: a window made later would be in
# front of the test copy's tab and panel.
$backdrop = if ($ShotDir) { Start-Backdrop -X ($prim.R - 470) -Y ([int]($prim.B / 2) - 240) -Width 470 -Height 480 -Seconds 25 }
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0', 'language=en')
try {
    $dialog = Open-Settings $app
    Select-Combo $dialog 120 1 # Español
    Click-Button $dialog 109
    Start-Sleep -Milliseconds 600
    Check '2. Español + Save: config.txt says language=es' ((Read-Config $app) -match '(?m)^language=es\r?$')
    Check '   and Settings closed' ((Find-Window $app 'EdgeDeckSettingsWindow') -eq [IntPtr]::Zero)
    if ($ShotDir) {
        $tab = @(Get-TestTabs $app)[0]
        Move-Mouse ($prim.R / 2) ($prim.B / 2); Start-Sleep -Milliseconds 200
        Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2); Start-Sleep -Milliseconds 900
        $panel = Get-TestPanel $app
        if ($panel) { Take-Shot -X $panel.L -Y $panel.T -Width ($panel.R - $panel.L) -Height ($panel.B - $panel.T) -Path (Join-Path $ShotDir 'settings-language-es.png') }
        Check '   the panel opens after the change' ([bool]$panel)
        Move-Mouse ($prim.R / 2) ($prim.B / 2)
    }
} finally {
    Stop-TestApp $app
    if ($backdrop) { Stop-Process -Id $backdrop.Id -Force -ErrorAction SilentlyContinue }
}

# ---- 3. the Shortcuts window ------------------------------------------------------------------
foreach ($probe in @(@{ M = $MOD_CONTROL -bor $MOD_ALT; K = 0x50 }, @{ M = $MOD_CONTROL -bor $MOD_ALT; K = 0x4B })) {
    if (-not (Test-ChordFree $probe.M $probe.K)) { throw 'Ctrl+Alt+P or Ctrl+Alt+K is taken before the test: close the real EdgeDeck (or whatever holds it) first.' }
}
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0')
try {
    Check '3. control: the test copy holds Ctrl+Alt+P (pause)' (-not (Test-ChordFree ($MOD_CONTROL -bor $MOD_ALT) 0x50))

    # 3a. Backspace leaves the pause box without a shortcut, and the keys are free once saved.
    $settings = Open-Settings $app
    Click-Button $settings 121 # Shortcuts...
    [void](Wait-Until -TimeoutSeconds 5 -PollMs 100 -Condition { (Find-Window $app 'EdgeDeckShortcutsWindow') -ne [IntPtr]::Zero })
    $keys = Find-Window $app 'EdgeDeckShortcutsWindow'
    Focus-Control $keys 301
    Press-Keys @(0x08) # Backspace
    if ($ShotDir) {
        $w = @(Get-Wins 'EdgeDeckShortcutsWindow' -ProcessId $app.Process.Id)[0]
        Take-Shot -X $w.L -Y $w.T -Width ($w.R - $w.L) -Height ($w.B - $w.T) -Path (Join-Path $ShotDir 'shortcuts-after-backspace.png')
    }
    Click-Button $keys 304 # Save
    Start-Sleep -Milliseconds 500
    Check '3a. Backspace + Save: config says shortcutPause=none' ((Read-Config $app) -match '(?m)^shortcutPause=none\r?$') ((Read-Config $app) -split "`n" | Select-String '^shortcutPause')
    Check '    and Ctrl+Alt+P is free again' (Test-ChordFree ($MOD_CONTROL -bor $MOD_ALT) 0x50)

    # 3b. A new chord for exit: registered, and the old one let go.
    $keys = Find-Window $app 'EdgeDeckShortcutsWindow'
    if ($keys -eq [IntPtr]::Zero) { Click-Button (Open-Settings $app) 121; Start-Sleep -Milliseconds 600; $keys = Find-Window $app 'EdgeDeckShortcutsWindow' }
    Focus-Control $keys 300
    Press-Keys @(0x11, 0x12, 0x4B) # Ctrl+Alt+K
    if ($ShotDir) {
        $w = @(Get-Wins 'EdgeDeckShortcutsWindow' -ProcessId $app.Process.Id)[0]
        Take-Shot -X $w.L -Y $w.T -Width ($w.R - $w.L) -Height ($w.B - $w.T) -Path (Join-Path $ShotDir 'shortcuts-typed.png')
    }
    Click-Button $keys 304
    Start-Sleep -Milliseconds 500
    Check '3b. exit set to Ctrl+Alt+K: config says so' ((Read-Config $app) -match '(?m)^shortcutExit=Ctrl\+Alt\+K\r?$') ((Read-Config $app) -split "`n" | Select-String '^shortcutExit')
    Check '    the test copy now holds Ctrl+Alt+K' (-not (Test-ChordFree ($MOD_CONTROL -bor $MOD_ALT) 0x4B))
    Check '    and Ctrl+Alt+Q is free' (Test-ChordFree ($MOD_CONTROL -bor $MOD_ALT) 0x51)

    # 3c. Keys another program holds are said in the window, which stays open to change them. A
    # single chord cannot be typed while someone holds it (Windows hands the keys to its owner, not
    # to the box), so it is a family: this script holds Ctrl+Shift+3, and the tabs are moved to
    # Ctrl+Shift+1..9.
    [void][Dlg]::RegisterHotKey([IntPtr]::Zero, 0x6E02, ($MOD_CONTROL -bor 4), 0x33)
    try {
        $keys = Find-Window $app 'EdgeDeckShortcutsWindow'
        if ($keys -eq [IntPtr]::Zero) { Click-Button (Open-Settings $app) 121; Start-Sleep -Milliseconds 600; $keys = Find-Window $app 'EdgeDeckShortcutsWindow' }
        Select-Combo $keys 302 2 # Ctrl+Shift+1..9
        Click-Button $keys 304
        Start-Sleep -Milliseconds 500
        $still = Find-Window $app 'EdgeDeckShortcutsWindow'
        Check '3c. keys another program holds: the window stays open' ($still -ne [IntPtr]::Zero)
        $status = if ($still -ne [IntPtr]::Zero) { [Dlg]::Statics($still) } else { '' }
        Check '    and its status line names them' ($status -match 'Ctrl\+Shift\+1\.\.9') $status
        if ($ShotDir -and $still -ne [IntPtr]::Zero) {
            $w = @(Get-Wins 'EdgeDeckShortcutsWindow' -ProcessId $app.Process.Id)[0]
            Take-Shot -X $w.L -Y $w.T -Width ($w.R - $w.L) -Height ($w.B - $w.T) -Path (Join-Path $ShotDir 'shortcuts-taken.png')
        }
        Check '    the rest of the family is still registered' (-not (Test-ChordFree ($MOD_CONTROL -bor 4) 0x34))
    } finally { [void][Dlg]::UnregisterHotKey([IntPtr]::Zero, 0x6E02) }
    # 3d. Off for the tab family frees its keys (Ctrl+Shift+1..9 since 3c).
    Check '3d. control: the test copy holds Ctrl+Shift+4' (-not (Test-ChordFree ($MOD_CONTROL -bor 4) 0x34))
    $keys = Find-Window $app 'EdgeDeckShortcutsWindow'
    if ($keys -eq [IntPtr]::Zero) { Click-Button (Open-Settings $app) 121; Start-Sleep -Milliseconds 600; $keys = Find-Window $app 'EdgeDeckShortcutsWindow' }
    Select-Combo $keys 302 6 # Off
    Focus-Control $keys 301
    Press-Keys @(0x08) # pause off again, so this Save has nothing taken in it
    Click-Button $keys 304
    Start-Sleep -Milliseconds 500
    Check '    Off + Save: config says shortcutTabs=none' ((Read-Config $app) -match '(?m)^shortcutTabs=none\r?$') ((Read-Config $app) -split "`n" | Select-String '^shortcutTabs')
    Check '    and Ctrl+Shift+4 is free' (Test-ChordFree ($MOD_CONTROL -bor 4) 0x34)
} finally {
    Move-Mouse ($prim.R / 2) ($prim.B / 2)
    Stop-TestApp $app
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

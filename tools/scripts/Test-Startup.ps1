<#
.SYNOPSIS
    The ways EdgeDeck can be started: for the first time, with a settings file it cannot read or
    that a newer version wrote, straight from a zip (a temporary folder), and a second time while
    it already runs.

.DESCRIPTION
      1. First start (no config.txt): the three tabs that work on any machine, and the welcome;
         the next start says nothing.
      2. A config.txt that is not settings: the defaults, the file copied to config.backup.txt
         before anything replaces it, and the log says so.
      3. A config.txt from a newer version: read as far as it can be, copied aside, and a Save
         marks the file with this version's format.
      4. Run from a temporary folder (what Explorer does with a program opened inside a zip): the
         log says so, and Start with Windows in Settings refuses to point there.
      5. EdgeDeck.exe started while it already runs: the second copy quits at once and the first
         shows its Settings window.

    Test copies only, each with LOCALAPPDATA in a scratch folder. Case 5 runs the real
    EdgeDeck.exe from the build (it is the one with the single-instance lock), so the user's own
    EdgeDeck must be closed: it would be the copy that answers. Notifications cannot be read from
    outside, so what a start decided is read from its log.
#>
[CmdletBinding()]
param([string]$ToolsDir, [string]$ExePath)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

$failures = 0
function Check($label, $ok, $detail = '') {
    '{0} {1}{2}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $(if (-not $ok -and $detail) { "  [$detail]" } else { '' })
    if (-not $ok) { $script:failures++ }
}
if (-not ('SD.U' -as [type])) {
    Add-Type -Namespace SD -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern System.IntPtr GetDlgItem(System.IntPtr d, int id); [DllImport("user32.dll")] public static extern System.IntPtr SendMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);'
}
function Log-Text($app) { if (Test-Path $app.Log) { Get-Content $app.Log -Raw } else { '' } }
function Backup-Path($app) { Join-Path $app.AppData 'EdgeDeck\config.backup.txt' }

# ---- 1. first start ---------------------------------------------------------------------------
$app = Start-TestApp -FirstRun -StartupSeconds 4
try {
    $tabs = @(Get-TestTabs $app)
    Check '1. first start: three tabs' ($tabs.Count -eq 3) "found $($tabs.Count)"
    Check '   and the welcome is shown' ((Log-Text $app) -match 'First start on this account')
    Check '   and no settings problem is reported' ((Log-Text $app) -notmatch 'Config:')
} finally { Stop-TestApp $app -KeepScratch }
# The same profile again: a second start, now with whatever the first one left.
$real = $env:LOCALAPPDATA; $env:LOCALAPPDATA = $app.AppData
try { $again = Start-Process -FilePath (Get-Tool apptest $ToolsDir) -ArgumentList "`"$($app.Trigger)`"" -PassThru } finally { $env:LOCALAPPDATA = $real }
Start-Sleep -Seconds 3
$second = [pscustomobject]@{ Process = $again; Scratch = $app.Scratch; AppData = $app.AppData; Trigger = $app.Trigger; Log = $app.Log }
try {
    $welcomes = ([regex]::Matches((Log-Text $second), 'First start on this account')).Count
    $hasConfig = Test-Path (Join-Path $app.AppData 'EdgeDeck\config.txt')
    # Without a config.txt yet, the second start is still a first one; with one, it is not.
    Check '   the second start welcomes again only if nothing was saved' (($hasConfig -and $welcomes -eq 1) -or (-not $hasConfig -and $welcomes -eq 2)) "config.txt: $hasConfig, welcomes: $welcomes"
} finally { Stop-TestApp $second }

# ---- 2. a file that is not settings -----------------------------------------------------------
$garbage = "This is my shopping list.`r`nMilk`r`nBread`r`n"
$app = Start-TestApp -ConfigText $garbage -StartupSeconds 3
try {
    Check '2. unreadable config: the default three tabs' (@(Get-TestTabs $app).Count -eq 3)
    Check '   the log says it could not be read' ((Log-Text $app) -match 'Config: could not be read')
    Check '   config.backup.txt holds the file as it was' ((Test-Path (Backup-Path $app)) -and ((Get-Content (Backup-Path $app) -Raw) -eq $garbage))
    # Something may save at startup (the one-time Start with Windows note does): by then the copy
    # exists, and what is written is settings.
    $now = Get-Content (Join-Path $app.AppData 'EdgeDeck\config.txt') -Raw
    Check '   config.txt is left as it was, or replaced by real settings' (($now -eq $garbage) -or ($now -match '(?m)^\[settings\]')) $now
} finally { Stop-TestApp $app }

# ---- 3. a newer version's file ----------------------------------------------------------------
$newer = "[settings]`nconfigVersion=2`ntheme=Light`nhologram=on`n[tab]`nwidget=Volume`nverticalRatio=0.3`n[gadget]`nsize=3`n"
$app = Start-TestApp -ConfigText $newer -StartupSeconds 3
try {
    Check '3. newer config: its one tab is used' (@(Get-TestTabs $app).Count -eq 1)
    Check '   the log says a newer version wrote it' ((Log-Text $app) -match 'written by a newer version')
    Check '   and it is copied aside' ((Test-Path (Backup-Path $app)) -and ((Get-Content (Backup-Path $app) -Raw) -match '\[gadget\]'))
    # Saving from Settings writes this version's format.
    Set-Content -Path (Join-Path $app.Trigger 'settings.txt') -Value 'x'
    [void](Wait-Until -TimeoutSeconds 5 -PollMs 100 -Condition { [bool](@(Get-Wins 'EdgeDeckSettingsWindow' -ProcessId $app.Process.Id | Where-Object Visible)) })
    $dialog = [IntPtr][long](@(Get-Wins 'EdgeDeckSettingsWindow' -ProcessId $app.Process.Id)[0].Hwnd)
    [void][SD.U]::SendMessage([SD.U]::GetDlgItem($dialog, 109), 0xF5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 600
    $saved = Get-Content (Join-Path $app.AppData 'EdgeDeck\config.txt') -Raw
    Check '   a Save writes configVersion=1' ($saved -match '(?m)^configVersion=1\r?$')
    Check '   and keeps what it understood (theme, the Volume tab)' (($saved -match '(?m)^theme=Light\r?$') -and ($saved -match '(?m)^widget=Volume\r?$'))
} finally { Stop-TestApp $app }

# ---- 4. run from a temporary folder -----------------------------------------------------------
$zipLike = Join-Path ([IO.Path]::GetTempPath()) ('Temp1_EdgeDeck-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 6) + '.zip')
New-Item -ItemType Directory -Path $zipLike | Out-Null
Copy-Item (Get-Tool apptest $ToolsDir) (Join-Path $zipLike 'apptest.exe')
$app = Start-TestApp -Tabs QuickActions -Exe (Join-Path $zipLike 'apptest.exe') -StartupSeconds 3
try {
    Check '4. started from a temporary folder: the log says so' ((Log-Text $app) -match 'Started from a temporary folder')
    Check '   and the welcome is not what is said (the temporary-folder note is)' ((Log-Text $app) -notmatch 'First start on this account')
    $run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $before = (Get-ItemProperty -Path $run -Name EdgeDeck -ErrorAction SilentlyContinue).EdgeDeck
    Set-Content -Path (Join-Path $app.Trigger 'settings.txt') -Value 'x'
    [void](Wait-Until -TimeoutSeconds 5 -PollMs 100 -Condition { [bool](@(Get-Wins 'EdgeDeckSettingsWindow' -ProcessId $app.Process.Id | Where-Object Visible)) })
    $dialog = [IntPtr][long](@(Get-Wins 'EdgeDeckSettingsWindow' -ProcessId $app.Process.Id)[0].Hwnd)
    $box = [SD.U]::GetDlgItem($dialog, 112)
    [void][SD.U]::SendMessage($box, 0xF1, [IntPtr]1, [IntPtr]::Zero) # BM_SETCHECK: Start with Windows
    [void][SD.U]::SendMessage([SD.U]::GetDlgItem($dialog, 109), 0xF5, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 600
    $after = (Get-ItemProperty -Path $run -Name EdgeDeck -ErrorAction SilentlyContinue).EdgeDeck
    Check '   Start with Windows does not point into the temporary folder' (-not ($after -like "*$zipLike*")) "Run = $after"
    Check '   and the startup entry is left as it was' ($after -eq $before) "before '$before', after '$after'"
} finally { Stop-TestApp $app; Remove-Item $zipLike -Recurse -Force -ErrorAction SilentlyContinue }
$app = Start-TestApp -Tabs QuickActions -StartupSeconds 3
try { Check '   control: from the build folder, nothing about a temporary folder' ((Log-Text $app) -notmatch 'temporary folder') } finally { Stop-TestApp $app }

# ---- 5. a second copy while one runs ----------------------------------------------------------
if (-not $ExePath) { $ExePath = Join-Path (Split-Path (Split-Path (Get-ToolsDir $ToolsDir))) 'Release\EdgeDeck.exe' }
if (Get-Process -Name EdgeDeck -ErrorAction SilentlyContinue) {
    '5 skipped: an EdgeDeck is already running (close it: it would be the copy that answers).'
} elseif (-not (Test-Path $ExePath)) {
    "5 skipped: no EdgeDeck.exe at $ExePath (pass -ExePath)."
} else {
    $scratch = New-ScratchDir
    $appdata = Join-Path $scratch 'appdata'
    New-Item -ItemType Directory -Path "$appdata\EdgeDeck" -Force | Out-Null
    [IO.File]::WriteAllText("$appdata\EdgeDeck\config.txt", "[settings]`ntheme=Dark`n[tab]`nwidget=QuickActions`n", (New-Object Text.UTF8Encoding $false))
    $real = $env:LOCALAPPDATA; $env:LOCALAPPDATA = $appdata
    try {
        $first = Start-Process -FilePath $ExePath -PassThru
        Start-Sleep -Seconds 3
        $secondCopy = Start-Process -FilePath $ExePath -PassThru
    } finally { $env:LOCALAPPDATA = $real }
    try {
        Check '5. the second copy quits at once' ($secondCopy.WaitForExit(4000))
        Check '   the first is still running' (-not $first.HasExited)
        $shown = Wait-Until -TimeoutSeconds 4 -PollMs 100 -Condition { [bool](@(Get-Wins 'EdgeDeckSettingsWindow' -ProcessId $first.Id | Where-Object Visible)) }
        Check '   and the first shows its Settings window' $shown
        Check '   its log says why' ((Get-Content "$appdata\EdgeDeck\edgedeck.log" -Raw) -match 'Started again while running')
    } finally {
        # Its own Exit: the hidden window's WM_CLOSE, as an installer would ask.
        $utility = @(Get-Wins 'EdgeDeckUtilityWindow' -ProcessId $first.Id)[0]
        if ($utility) {
            [void][SD.U]::SendMessage([IntPtr][long]$utility.Hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        }
        Check '   the first exits cleanly when its window is closed' ($first.WaitForExit(5000))
        if (-not $first.HasExited) { Stop-Process -Id $first.Id -Force }
        Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

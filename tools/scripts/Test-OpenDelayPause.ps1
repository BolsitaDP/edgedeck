<#
.SYNOPSIS
    Checks the open delay, the pause and the menu's "Restart EdgeDeck" on a test copy of EdgeDeck.

.DESCRIPTION
      Open delay (openDelayMs=600)
        - a pointer that rests on a tab for less than the delay and leaves opens nothing;
        - one that stays opens the panel, not before the delay and not long after it;
        - with no delay (the default) the panel opens at once - the control that shows the waiting
          above is the delay and not just a slow machine.

      Pause (the command the tray menu and Ctrl+Alt+P run, driven through apptest's pause.txt)
        - pausing hides the tab, and the pointer where it was opens nothing;
        - resuming brings it back and it opens again.

      Restart (apptest's restart.txt)
        - the old copy exits, a new one with the same command line takes over, and nothing is left
          running twice.

    Moves the real cursor. Starts and stops its own apptest.exe copies; safe beside the real
    EdgeDeck, whose tabs are never touched (windows and processes are found by the test copy's pid
    and trigger folder).
#>
[CmdletBinding()]
param([string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

$failures = 0
function Check($label, $ok, $detail = '') {
    '{0} {1}{2}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $(if (-not $ok -and $detail) { "  [$detail]" } else { '' })
    if (-not $ok) { $script:failures++ }
}
$prim = Get-PrimaryRect
function Park { Move-Mouse ($prim.R / 2) ($prim.B / 2) }
function TabCenter($tab) { @{ X = ($tab.L + $tab.R) / 2; Y = ($tab.T + $tab.B) / 2 } }

# Milliseconds from putting the pointer on the tab until a panel is visible, or -1.
function Time-ToOpen($app, $tab, [int]$limitMs) {
    Park; Start-Sleep -Milliseconds 700 # let any earlier panel close
    $c = TabCenter $tab
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Move-Mouse $c.X $c.Y
    while ($sw.ElapsedMilliseconds -lt $limitMs) {
        if (Get-TestPanel $app) { return [int]$sw.ElapsedMilliseconds }
        Start-Sleep -Milliseconds 15
    }
    return -1
}

# ---- open delay -------------------------------------------------------------------------------
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0', 'openDelayMs=600')
try {
    $tab = @(Get-TestTabs $app)[0]
    $c = TabCenter $tab

    # Rests for a third of the delay and goes away.
    Park; Start-Sleep -Milliseconds 500
    Move-Mouse $c.X $c.Y
    Start-Sleep -Milliseconds 200
    Park
    Start-Sleep -Milliseconds 1200 # well past when it would have opened
    Check 'a pointer that leaves before the delay opens nothing' (-not (Get-TestPanel $app))

    $ms = Time-ToOpen $app $tab 3000
    Check 'a pointer that stays opens the panel' ($ms -ge 0) 'it never opened'
    Check 'not before the delay (600 ms)' ($ms -ge 500) "$ms ms"
    Check 'and not long after it' ($ms -ge 0 -and $ms -le 1400) "$ms ms"
} finally {
    Park
    Stop-TestApp $app
}

$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0')
try {
    $tab = @(Get-TestTabs $app)[0]
    $ms = Time-ToOpen $app $tab 2000
    Check 'control: with no delay the panel opens at once' ($ms -ge 0 -and $ms -le 450) "$ms ms"
} finally {
    Park
    Stop-TestApp $app
}

# ---- pause ------------------------------------------------------------------------------------
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0')
try {
    $tab = @(Get-TestTabs $app)[0]
    $c = TabCenter $tab
    Check 'the tab is visible before pausing' (@(Get-TestTabs $app).Count -eq 1)

    Set-Content -Path (Join-Path $app.Trigger 'pause.txt') -Value 'x'
    $hidden = Wait-Until -TimeoutSeconds 4 -PollMs 100 -Condition { @(Get-TestTabs $app).Count -eq 0 }
    Check 'pausing hides the tab' $hidden

    Park; Start-Sleep -Milliseconds 300
    Move-Mouse $c.X $c.Y
    Start-Sleep -Milliseconds 1000
    Check 'and the pointer where it was opens nothing' (-not (Get-TestPanel $app))
    Check 'the tooltip says so (the log notes the pause)' ((Get-Content $app.Log -Raw) -match 'Tabs paused')

    Set-Content -Path (Join-Path $app.Trigger 'pause.txt') -Value 'x'
    $back = Wait-Until -TimeoutSeconds 4 -PollMs 100 -Condition { @(Get-TestTabs $app).Count -eq 1 }
    Check 'resuming brings the tab back' $back
    $ms = Time-ToOpen $app (@(Get-TestTabs $app)[0]) 2000
    Check 'and it opens again' ($ms -ge 0) 'it never opened'
} finally {
    Park
    Stop-TestApp $app
}

# ---- restart ----------------------------------------------------------------------------------
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0')
$replacement = $null
try {
    $old = $app.Process
    Set-Content -Path (Join-Path $app.Trigger 'restart.txt') -Value 'x'
    Check 'the old copy exits' ($old.WaitForExit(12000))

    $replacement = $null
    [void](Wait-Until -TimeoutSeconds 12 -PollMs 200 -Condition {
        $script:replacement = Get-CimInstance Win32_Process -Filter "Name='apptest.exe'" |
            Where-Object { $_.CommandLine -like "*$($app.Trigger)*" -and $_.ProcessId -ne $old.Id } | Select-Object -First 1
        [bool]$script:replacement
    })
    Check 'a new copy is running' ([bool]$replacement)
    if ($replacement) {
        Check 'with the same arguments, and told which copy it replaces' ($replacement.CommandLine -match [regex]::Escape($app.Trigger) -and $replacement.CommandLine -match '--previous-pid=\d+')
        Check 'and not told it was started by a crash' ($replacement.CommandLine -notmatch 'restarted-after-crash')
        Start-Sleep -Seconds 3
        Check 'it has its tab up' (@(Get-Wins 'EdgeDeckTabWindow' -ProcessId $replacement.ProcessId | Where-Object Visible).Count -eq 1)
        Check 'and only one copy is running' (@(Get-CimInstance Win32_Process -Filter "Name='apptest.exe'" | Where-Object { $_.CommandLine -like "*$($app.Trigger)*" }).Count -eq 1)
        Check 'the log does not call it a crash' (-not ((Get-Content $app.Log -Raw) -match 'crashed'))
    }
} finally {
    Get-CimInstance Win32_Process -Filter "Name='apptest.exe'" | Where-Object { $_.CommandLine -like "*$($app.Trigger)*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Remove-Item $app.Scratch -Recurse -Force -ErrorAction SilentlyContinue
    Park
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

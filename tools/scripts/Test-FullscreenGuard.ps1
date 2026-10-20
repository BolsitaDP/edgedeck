<#
.SYNOPSIS
    Checks that a tab steps aside (is hidden) while a full-screen app is in front of its monitor,
    comes back when the app is gone, and that its panel opens on a hover in every other case.

.DESCRIPTION
    Starts a test copy of EdgeDeck (apptest.exe, scratch config) and, in four situations,
    hovers its tab with the real cursor and reports whether the panel opened:

      A. a borderless window covers the primary monitor, guard on  -> tab hidden, nothing opens
      B. that window gone again, guard on                          -> tab back, must open
      C. the same window, guard switched off in the config         -> tab stays, must open
      D. a window like a maximized one (work area, title bar on top), guard on -> must open

    A plain dark window covers the primary monitor for about ten seconds in total. It moves
    the real cursor. The real EdgeDeck can stay open; the test copy's tab is picked out by
    process, not by position.
#>
[CmdletBinding()]
param([string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

$fullscreenwin = Get-Tool fullscreenwin $ToolsDir
$primary = Get-PrimaryRect
$work = Get-WorkArea
$failures = 0

# What the client area of an ordinary maximized window looks like: the work area with a title
# bar's worth missing at the top. The work area alone would not do - on a monitor with no
# taskbar (or an auto-hiding one) it is the whole monitor, and a borderless window that size
# really is indistinguishable from a full-screen one.
$maximizedClient = [pscustomobject]@{ L = $work.L; T = $work.T + 32; R = $work.R; B = $work.B }

function Open-On-Hover($app) {
    $tabs = @(Get-TestTabs $app)
    if ($tabs.Count -eq 0) { return $false } # hidden: there is nothing to hover
    $tab = $tabs[0]
    Move-Mouse ($primary.R / 2) ($primary.B / 2); Start-Sleep -Milliseconds 500   # make sure the pointer ENTERS
    Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2); Start-Sleep -Milliseconds 1100
    return [bool](Get-TestPanel $app)
}

function Start-Cover($rect, $seconds) {
    $out = [IO.Path]::GetTempFileName()
    $p = Start-Process -FilePath $fullscreenwin -PassThru -RedirectStandardOutput $out `
         -ArgumentList "$($rect.L) $($rect.T) $($rect.R - $rect.L) $($rect.B - $rect.T) $seconds"
    Start-Sleep -Milliseconds 900
    $foreground = (Get-Content $out -ErrorAction SilentlyContinue) -join ' '
    Remove-Item $out -Force -ErrorAction SilentlyContinue
    if ($foreground -notmatch 'foreground=1') { Write-Warning "the cover window did not become the foreground window; the result below is not meaningful" }
    return $p
}

# Whether the test copy's tab is on screen. The shell's full-screen notice and EdgeDeck's second
# look come within about half a second of the cover window appearing or going.
function Tab-Shown($app) { Start-Sleep -Milliseconds 700; return [bool](Get-TestTabs $app) }

function Check-Tab($label, $expected, $actual) {
    $ok = ($expected -eq $actual)
    '{0} {1}: tab shown = {2} (expected {3})' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $actual, $expected
    if (-not $ok) { $script:failures++ }
}

function Check($label, $expected, $actual) {
    $ok = ($expected -eq $actual)
    '{0} {1}: panel opened = {2} (expected {3})' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $actual, $expected
    if (-not $ok) { $script:failures++ }
}

# A and B: guard on (the default).
$app = Start-TestApp -Tabs QuickActions
try {
    Check-Tab 'before: no full-screen app           ' $true (Tab-Shown $app)
    $cover = Start-Cover $primary 14
    Check-Tab 'A. full-screen app in front, guard on ' $false (Tab-Shown $app)
    Check 'A. full-screen app in front, guard on ' $false (Open-On-Hover $app)
    Stop-Process -Id $cover.Id -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 800
    Check-Tab 'B. full-screen app gone, guard on     ' $true (Tab-Shown $app)
    Check 'B. full-screen app gone, guard on     ' $true (Open-On-Hover $app)
} finally { Move-Mouse ($primary.R / 2) ($primary.B / 2); Stop-TestApp $app; Stop-Process -Name fullscreenwin -Force -ErrorAction SilentlyContinue }

# C: guard off.
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0')
try {
    $cover = Start-Cover $primary 10
    Check-Tab 'C. full-screen app in front, guard off' $true (Tab-Shown $app)
    Check 'C. full-screen app in front, guard off' $true (Open-On-Hover $app)
} finally { Move-Mouse ($primary.R / 2) ($primary.B / 2); Stop-TestApp $app; Stop-Process -Name fullscreenwin -Force -ErrorAction SilentlyContinue }

# D: an ordinary maximized window (title bar present), guard on.
$app = Start-TestApp -Tabs QuickActions
try {
    $cover = Start-Cover $maximizedClient 10
    Check-Tab 'D. ordinary maximized window, guard on' $true (Tab-Shown $app)
    Check 'D. ordinary maximized window, guard on' $true (Open-On-Hover $app)
} finally { Move-Mouse ($primary.R / 2) ($primary.B / 2); Stop-TestApp $app; Stop-Process -Name fullscreenwin -Force -ErrorAction SilentlyContinue }

# E: switching away from a full-screen app (Alt+Tab, a click elsewhere) brings the tab back while
# the app is still open; coming back to it hides the tab again.
$app = Start-TestApp -Tabs QuickActions
try {
    $cover = Start-Cover $primary 16
    Check-Tab 'E. full-screen app in front                  ' $false (Tab-Shown $app)
    $small = [pscustomobject]@{ L = $primary.L + 200; T = $primary.T + 200; R = $primary.L + 700; B = $primary.T + 600 }
    $other = Start-Cover $small 4 # an ordinary window, now in front of the full-screen one
    Check-Tab 'E. another window in front of it             ' $true (Tab-Shown $app)
    $other.WaitForExit(6000) | Out-Null
    Start-Sleep -Milliseconds 300
    Check-Tab 'E. that window gone, the full-screen one back' $false (Tab-Shown $app)
} finally { Move-Mouse ($primary.R / 2) ($primary.B / 2); Stop-TestApp $app; Stop-Process -Name fullscreenwin -Force -ErrorAction SilentlyContinue }

# F: a full-screen app on another monitor hides only that monitor's tabs.
Add-Type -AssemblyName System.Windows.Forms
$second = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
$secondId = $null
if ($second) {
    foreach ($line in (& (Get-Tool dispcfg $ToolsDir) list)) {
        if ($line -match 'id=(\S+)\s+name=.*?\s+active=1\s.*gdi=(\S+)' -and $Matches[2] -eq $second.DeviceName) { $secondId = $Matches[1] }
    }
}
if (-not $secondId) {
    'F skipped: no second monitor is on.'
} else {
    $b = $second.Bounds
    $rect = [pscustomobject]@{ L = $b.Left; T = $b.Top; R = $b.Right; B = $b.Bottom }
    $app = Start-TestApp -Tabs @('QuickActions', 'QuickActions') -TabExtra @('', "monitor=$secondId")
    try {
        $both = @(Get-TestTabs $app).Count
        Check-Tab 'F. both tabs there to begin with             ' $true ($both -eq 2)
        $cover = Start-Cover $rect 10
        Start-Sleep -Milliseconds 700
        $shown = @(Get-TestTabs $app)
        $onPrimary = [bool]($shown | Where-Object { $_.R -eq $primary.R })
        $onSecond = [bool]($shown | Where-Object { $_.L -ge $b.Left -and $_.R -le $b.Right })
        Check-Tab 'F. full-screen on the other monitor: its tab ' $false $onSecond
        Check-Tab 'F. ...while the primary monitor keeps its tab' $true $onPrimary
        Stop-Process -Id $cover.Id -Force -ErrorAction SilentlyContinue # as a game that crashed
        Start-Sleep -Milliseconds 1200
        Check-Tab 'F. the other monitor''s tab back afterwards   ' $true ((@(Get-TestTabs $app)).Count -eq 2)
    } finally { Move-Mouse ($primary.R / 2) ($primary.B / 2); Stop-TestApp $app; Stop-Process -Name fullscreenwin -Force -ErrorAction SilentlyContinue }
}

if ($failures) { Write-Host "$failures case(s) failed."; exit 1 }
Write-Host 'All cases passed.'

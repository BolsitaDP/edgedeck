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
    $tab = (Get-TestTabs $app)[0]
    if (-not $tab) { return $false } # hidden: there is nothing to hover
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

if ($failures) { Write-Host "$failures case(s) failed."; exit 1 }
Write-Host 'All cases passed.'

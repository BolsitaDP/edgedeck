<#
.SYNOPSIS
    Checks the move handle in a panel's top-left corner: holding it and dragging moves the tab and
    its panel up and down their edge, they stop at the monitor's top and bottom, and where the tab
    is let go is what config.txt says - to the pixel - so it is there again after a restart.

.DESCRIPTION
    Runs a test copy of EdgeDeck (apptest.exe) with one tab on the primary monitor's right edge,
    opens its panel, presses the left button on the handle and drags with the real cursor. Then
    restarts the copy on the config it saved and checks the tab comes back at the same top.

    Moves the real cursor and holds the left button for a few seconds. Safe beside the real
    EdgeDeck (the test copy's windows are found by process).
#>
[CmdletBinding()]
param([string]$ToolsDir, [ValidateSet('right', 'left')][string]$Edge = 'right')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"
Add-Type -AssemblyName System.Windows.Forms

$failures = 0
function Check($label, $ok, $detail = '') {
    '{0} {1}{2}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $(if (-not $ok -and $detail) { "  [$detail]" } else { '' })
    if (-not $ok) { $script:failures++ }
}

$monitor = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$park = @{ X = $monitor.X + [int]($monitor.Width / 2); Y = $monitor.Y + [int]($monitor.Height / 2) }

function Open-Panel($app) {
    $tab = @(Get-TestTabs $app)[0]
    Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2)
    [void](Wait-Until -TimeoutSeconds 3 -PollMs 100 -Condition { [bool](Get-TestPanel $app) })
    Start-Sleep -Milliseconds 450 # after the 200 ms slide
    return Get-TestPanel $app
}

# The handle's centre, in screen pixels: 10..34 logical across, centred in the 44-pixel header.
function Get-HandlePoint($panel) {
    $scale = $panel.Dpi / 96
    [pscustomobject]@{ X = [int]($panel.L + 22 * $scale); Y = [int]($panel.T + 22 * $scale) }
}

# Holds the left button at (X, Y) and drags vertically by Dy in small steps, as a hand would.
function Drag-Vertically([int]$x, [int]$y, [int]$dy) {
    Move-Mouse $x $y
    Start-Sleep -Milliseconds 120
    [Ui]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero) # left down
    Start-Sleep -Milliseconds 80
    $steps = [math]::Max(1, [int]([math]::Abs($dy) / 8))
    for ($i = 1; $i -le $steps; $i++) { Move-Mouse $x ($y + [int]($dy * $i / $steps)); Start-Sleep -Milliseconds 8 }
    Start-Sleep -Milliseconds 120
}
function Release-Button { [Ui]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300 }

function Read-Ratio($app) {
    $text = Get-Content (Join-Path $app.AppData 'EdgeDeck\config.txt') -Raw
    if ($text -match '(?m)^verticalRatio=(\S+)') { return [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture) }
    return $null
}

$app = Start-TestApp -Tabs @('QuickActions') -TabExtra @("edge=$Edge") -Settings @('theme=Dark', 'fullscreenGuard=0') -StartupSeconds 3
$app2 = $null
try {
    $tab = @(Get-TestTabs $app)[0]
    $tabHeight = $tab.B - $tab.T
    $startTop = $tab.T
    $panel = Open-Panel $app
    Check 'the panel opens' ([bool]$panel)
    if (-not $panel) { throw 'no panel to drag' }
    $handle = Get-HandlePoint $panel
    $panelOffset = $panel.T - $tab.T

    # 1. Up by 200 pixels, checked while the button is still held: both windows follow.
    Drag-Vertically $handle.X $handle.Y -200
    $during = @(Get-TestTabs $app)[0]
    $panelDuring = Get-TestPanel $app
    Check 'while held, the tab follows the pointer' ($during.T -eq $startTop - 200) "tab top $($during.T), expected $($startTop - 200)"
    Check 'while held, the panel moves with it' ($panelDuring -and ($panelDuring.T - $during.T) -eq $panelOffset) "panel top $($panelDuring.T)"
    Check 'the tab stays on its edge' ($(if ($Edge -eq 'left') { $during.L -eq $monitor.Left } else { $during.R -eq $monitor.Right }))
    Release-Button
    Check 'the panel is still open after letting go' ([bool](Get-TestPanel $app))

    $ratio = Read-Ratio $app
    $expectedTop = $monitor.Y + [math]::Truncate(($monitor.Height - $tabHeight) * $ratio)
    Check 'the new place is saved, to the pixel' ($ratio -ne $null -and $expectedTop -eq $startTop - 200) "ratio $ratio gives top $expectedTop, tab is at $($startTop - 200)"

    # 2a. Far past the bottom: the tab goes all the way down, though the pointer meets the screen's
    # edge first (the handle is below the panel's top, and the panel stays on the monitor).
    $panel = Get-TestPanel $app
    $handle = Get-HandlePoint $panel
    Drag-Vertically $handle.X $handle.Y $monitor.Height
    Release-Button
    $bottom = @(Get-TestTabs $app)[0]
    Check 'dragged past the bottom, the tab reaches the bottom of the monitor' ($bottom.B -eq $monitor.Bottom) "tab bottom $($bottom.B)"
    Check 'the very bottom is saved as 1' ((Read-Ratio $app) -eq 1)

    # 2. Far past the top: the tab stops at the monitor's top edge.
    $panel = Get-TestPanel $app
    $handle = Get-HandlePoint $panel
    Drag-Vertically $handle.X $handle.Y (-$monitor.Height)
    Release-Button
    $top = @(Get-TestTabs $app)[0]
    Check 'dragged past the top, the tab stops at the top of the monitor' ($top.T -eq $monitor.Top) "tab top $($top.T)"
    Check 'and the panel stays on the monitor' ((Get-TestPanel $app).T -ge $monitor.Top)
    Check 'the very top is saved as 0' ((Read-Ratio $app) -eq 0)

    # 3. A click on the handle without moving changes nothing and saves nothing new.
    $before = Get-Content (Join-Path $app.AppData 'EdgeDeck\config.txt') -Raw
    $panel = Get-TestPanel $app
    $handle = Get-HandlePoint $panel
    Drag-Vertically $handle.X $handle.Y 0
    Release-Button
    Check 'a click without a move leaves the tab where it was' ((@(Get-TestTabs $app)[0]).T -eq $monitor.Top)
    Check 'and leaves config.txt as it was' ((Get-Content (Join-Path $app.AppData 'EdgeDeck\config.txt') -Raw) -eq $before)

    # 4. Down to somewhere in the middle, then a restart on the saved config.
    Drag-Vertically $handle.X $handle.Y 333
    Release-Button
    $placed = @(Get-TestTabs $app)[0]
    Move-Mouse $park.X $park.Y
    Check 'the panel closes once the pointer leaves' (Wait-Until -TimeoutSeconds 4 -PollMs 150 -Condition { -not (Get-TestPanel $app) })

    $config = Get-Content (Join-Path $app.AppData 'EdgeDeck\config.txt') -Raw
    Stop-TestApp $app; $app = $null
    $app2 = Start-TestApp -Raw $config -Tabs @() -Settings @() -StartupSeconds 3
    $again = @(Get-TestTabs $app2)[0]
    Check 'after a restart the tab is exactly where it was let go' ($again -and $again.T -eq $placed.T) "was $($placed.T), now $($again.T)"
} finally {
    [Ui]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero) # never leave the button held
    Move-Mouse $park.X $park.Y
    if ($app) { Stop-TestApp $app }
    if ($app2) { Stop-TestApp $app2 }
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

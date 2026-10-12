<#
.SYNOPSIS
    Checks that a panel is actually on screen after the pointer lands on its tab - slide included -
    without the pointer having to enter the panel.

.DESCRIPTION
    A panel window is "visible" to Windows (IsWindowVisible) whether or not anything is drawn in
    it, so the window-level checks of the other scripts cannot tell an open panel from an empty
    one. This looks at pixels: a plain window is put where the panel will open, the pointer is
    parked on the tab and the picture is sampled during and after the slide. A picture with more
    than one colour in it holds the panel.

    It exists because of a bug it would have caught: the panel is presented for the first time
    while it is still wholly outside the desktop, where DWM keeps no picture of a per-pixel-alpha
    window, so it stayed invisible until the pointer entered it and made the next paint happen.
    Nothing checked pixels, and moving the pointer into the panel - what anyone does next - hid it.

    Moves the real cursor and puts a plain window on the primary monitor's edge for ~15 s per edge.
    Safe beside the real EdgeDeck (the test copy's tabs are found by process).
#>
[CmdletBinding()]
param([string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"
Add-Type -AssemblyName System.Drawing

$failures = 0
function Check($label, $ok, $detail = '') {
    '{0} {1}{2}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $(if (-not $ok -and $detail) { "  [$detail]" } else { '' })
    if (-not $ok) { $script:failures++ }
}

$prim = Get-PrimaryRect
$w = 420; $h = 260
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$script:regionX = 0; $script:regionY = 0
# How many different colours a grid of the region holds. The plain window is one; the panel adds more.
# The columns next to the tab are left out, so the tab itself does not count.
function Colours {
    $g.CopyFromScreen($script:regionX, $script:regionY, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $seen = @{}
    for ($py = 5; $py -lt $h; $py += 9) { for ($px = 60; $px -lt $w - 60; $px += 9) { $seen[$bmp.GetPixel($px, $py).ToArgb()] = 1 } }
    $seen.Count
}

try {
    foreach ($edge in 'right', 'left') {
        $tabTop = [int](($prim.B - 76) * 0.5)
        $script:regionY = $tabTop + 38 - [int]($h / 2)
        $script:regionX = if ($edge -eq 'right') { $prim.R - $w } else { $prim.L }
        # The plain window goes up before the app: the newest topmost window is on top, and one
        # created afterwards would cover the tab and swallow the hover.
        $backdrop = Start-Backdrop -X $script:regionX -Y $script:regionY -Width $w -Height $h -Seconds 40 -Color '2B2B2B'
        $app = Start-TestApp -Tabs @('QuickActions') -TabExtra @("edge=$edge") -Settings @('theme=Dark', 'fullscreenGuard=0')
        try {
            $tab = @(Get-TestTabs $app)[0]
            Check "[$edge edge] the plain window shows one colour before the hover" ((Colours) -eq 1)

            Move-Mouse (($prim.L + $prim.R) / 2) ($prim.B / 2); Start-Sleep -Milliseconds 400
            Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2)
            Start-Sleep -Milliseconds 120
            $mid = Colours
            Start-Sleep -Milliseconds 500
            $settled = Colours
            Check "[$edge edge] the panel is drawn once the slide is over, with the pointer still on the tab" ($settled -gt 1) "$settled colour(s)"
            Check "[$edge edge] ...and was already drawn while it slid in" ($mid -gt 1) "$mid colour(s) at 120 ms"

            # Out and back: a second opening must be drawn too, not only the first.
            Move-Mouse (($prim.L + $prim.R) / 2) ($prim.B / 2)
            [void](Wait-Until -TimeoutSeconds 4 -PollMs 150 -Condition { -not (Get-TestPanel $app) })
            Start-Sleep -Milliseconds 300
            Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2)
            Start-Sleep -Milliseconds 700
            $again = Colours
            Check "[$edge edge] a second opening is drawn as well" ($again -gt 1) "$again colour(s)"
        } finally {
            Move-Mouse (($prim.L + $prim.R) / 2) ($prim.B / 2)
            Stop-TestApp $app
            Stop-Process -Id $backdrop.Id -Force -ErrorAction SilentlyContinue
        }
    }
} finally {
    $g.Dispose(); $bmp.Dispose()
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

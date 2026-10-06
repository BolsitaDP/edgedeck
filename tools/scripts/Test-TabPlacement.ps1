<#
.SYNOPSIS
    Checks, against the real desktop, that tabs dock to the edge and monitor they are configured
    for, that their panels open inside that monitor, and that a panel sliding in is not seen on a
    neighbouring monitor.

.DESCRIPTION
    Runs a test copy of EdgeDeck (apptest.exe) with tabs configured for the right and the left
    edge of the primary monitor and, when there is a second monitor switched on, for that one too -
    plus one for a monitor that is not connected, which must fall back to the primary. Each tab is
    hovered and where its panel opens is compared with where it should.

    Then, for a pair of side-by-side monitors, the slide itself: a plain window is put on the
    neighbour, the panel is opened from the edge that faces it, and that strip of the neighbour is
    captured over and over while the panel slides. Any pixel that is not the plain window's colour
    is the panel showing on the wrong monitor. A control makes sure the scan can see such a pixel.

    Moves the real cursor and opens plain windows on a second monitor for a few seconds. Does not
    switch monitors. Safe beside the real EdgeDeck (the test copy's tabs are found by process).
#>
[CmdletBinding()]
param([string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not ('PixelScan' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System.Drawing;
using System.Drawing.Imaging;
public static class PixelScan {
    // How many pixels differ from 0xRRGGBB by more than a rounding error.
    public static int CountDifferent(Bitmap bmp, int rgb) {
        var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int differ = 0;
        try {
            int r0 = (rgb >> 16) & 255, g0 = (rgb >> 8) & 255, b0 = rgb & 255;
            for (int y = 0; y < bmp.Height; y++) {
                for (int x = 0; x < bmp.Width; x++) {
                    int p = System.Runtime.InteropServices.Marshal.ReadInt32(data.Scan0, y * data.Stride + x * 4);
                    int b = p & 255, g = (p >> 8) & 255, r = (p >> 16) & 255;
                    if (System.Math.Abs(r - r0) > 2 || System.Math.Abs(g - g0) > 2 || System.Math.Abs(b - b0) > 2) differ++;
                }
            }
        } finally { bmp.UnlockBits(data); }
        return differ;
    }
}
"@
}

$failures = 0
function Check($label, $ok, $detail = '') {
    '{0} {1}{2}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $(if (-not $ok -and $detail) { "  [$detail]" } else { '' })
    if (-not $ok) { $script:failures++ }
}

# ---- which monitors are on, and where ---------------------------------------------------------
$screens = @{}
foreach ($s in [System.Windows.Forms.Screen]::AllScreens) { $screens[$s.DeviceName] = $s }
$monitors = @()
foreach ($line in (& (Get-Tool dispcfg $ToolsDir) list)) {
    if ($line -match 'id=(\S+)\s+name=.*?\s+active=1\s+native=\S+\s+target=\d+\s+gdi=(\S+)') {
        $screen = $screens[$Matches[2]]
        if ($screen) { $monitors += [pscustomobject]@{ Id = $Matches[1]; Screen = $screen; B = $screen.Bounds; Primary = $screen.Primary } }
    }
}
if ($monitors.Count -eq 0) { throw 'dispcfg reported no active monitor that Windows Forms also knows.' }
$primary = $monitors | Where-Object Primary | Select-Object -First 1
$other = $monitors | Where-Object { -not $_.Primary } | Select-Object -First 1
'Monitors on: ' + (($monitors | ForEach-Object { '{0} {1}x{2}@({3},{4}){5}' -f $_.Id, $_.B.Width, $_.B.Height, $_.B.X, $_.B.Y, $(if ($_.Primary) { ' primary' }) }) -join '; ')

# ---- 1. docking: the right edge and the left edge, on the primary and on another monitor --------
$cases = @(
    [pscustomobject]@{ Label = 'default (right edge, primary monitor)'; Extra = 'verticalRatio=0.10'; Mon = $primary; Edge = 'right'; Ratio = 0.10 }
    [pscustomobject]@{ Label = 'left edge of the primary monitor';      Extra = "verticalRatio=0.35`nedge=left"; Mon = $primary; Edge = 'left'; Ratio = 0.35 }
)
if ($other) {
    $cases += [pscustomobject]@{ Label = "right edge of monitor $($other.Id)"; Extra = "verticalRatio=0.55`nedge=right`nmonitor=$($other.Id)"; Mon = $other; Edge = 'right'; Ratio = 0.55 }
    $cases += [pscustomobject]@{ Label = "left edge of monitor $($other.Id)";  Extra = "verticalRatio=0.75`nedge=left`nmonitor=$($other.Id)";  Mon = $other; Edge = 'left';  Ratio = 0.75 }
}
$cases += [pscustomobject]@{ Label = 'a monitor that is not connected falls back to the primary'; Extra = "verticalRatio=0.90`nmonitor=ZZZ9999"; Mon = $primary; Edge = 'right'; Ratio = 0.90 }
if (-not $other) { 'Only one monitor is on: the second-monitor cases are skipped.' }

function Near($a, $b, $tol = 1) { [math]::Abs($a - $b) -le $tol }

$app = Start-TestApp -Tabs (@('QuickActions') * $cases.Count) -TabExtra ($cases | ForEach-Object Extra) `
                     -Settings @('theme=Dark', 'fullscreenGuard=0') -StartupSeconds 4
try {
    $tabs = @(Get-TestTabs $app)
    Check "all $($cases.Count) tabs exist" ($tabs.Count -eq $cases.Count) "found $($tabs.Count)"
    $park = [pscustomobject]@{ X = $primary.B.X + [int]($primary.B.Width / 2); Y = $primary.B.Y + [int]($primary.B.Height / 2) }

    foreach ($case in $cases) {
        $b = $case.Mon.B
        # The window that is where this case says it should be (its vertical place is derived
        # from its own height, as the app does it: the monitor height minus the tab, times the ratio).
        $want = $tabs | Where-Object {
            $h = $_.B - $_.T
            $top = $b.Y + [math]::Truncate(($b.Height - $h) * $case.Ratio)
            (Near $_.T $top) -and $(if ($case.Edge -eq 'right') { $_.R -eq $b.Right } else { $_.L -eq $b.Left })
        } | Select-Object -First 1
        Check "tab docked: $($case.Label)" ([bool]$want) 'no tab window at the expected place'
        if (-not $want) { continue }

        Move-Mouse (($want.L + $want.R) / 2) (($want.T + $want.B) / 2)
        [void](Wait-Until -TimeoutSeconds 3 -PollMs 100 -Condition { [bool](Get-TestPanel $app) })
        Start-Sleep -Milliseconds 450 # the slide is 200 ms; this is after it
        $panel = Get-TestPanel $app
        Check "  its panel opens" ([bool]$panel)
        if ($panel) {
            $besideTab = if ($case.Edge -eq 'right') { Near $panel.R $want.L } else { Near $panel.L $want.R }
            Check "  on the screen side of the tab" $besideTab ("panel {0}..{1}, tab {2}..{3}" -f $panel.L, $panel.R, $want.L, $want.R)
            $inside = $panel.L -ge $b.Left -and $panel.R -le $b.Right -and $panel.T -ge $b.Top -and $panel.B -le $b.Bottom
            Check "  inside its monitor" $inside ("panel ({0},{1})-({2},{3}) monitor ({4},{5})-({6},{7})" -f $panel.L, $panel.T, $panel.R, $panel.B, $b.Left, $b.Top, $b.Right, $b.Bottom)
        }
        Move-Mouse $park.X $park.Y
        $closed = Wait-Until -TimeoutSeconds 4 -PollMs 150 -Condition { -not (Get-TestPanel $app) }
        Check "  and closes when the pointer leaves" $closed
    }
} finally {
    Stop-TestApp $app
}

# ---- 2. the slide must not be seen on a neighbouring monitor ---------------------------------
# For every pair of monitors that touch side by side: a tab on A's edge that faces B, with B beyond
# it. The panel slides in from outside that edge, which is on B.
$pairs = @()
foreach ($a in $monitors) {
    foreach ($c in ($monitors | Where-Object { $_ -ne $a })) {
        $overlapTop = [math]::Max($a.B.Top, $c.B.Top); $overlapBottom = [math]::Min($a.B.Bottom, $c.B.Bottom)
        if ($overlapBottom - $overlapTop -lt 320) { continue }
        $edge = if ($c.B.Right -eq $a.B.Left) { 'left' } elseif ($c.B.Left -eq $a.B.Right) { 'right' } else { $null }
        if ($edge) { $pairs += [pscustomobject]@{ Tab = $a; Neighbour = $c; Edge = $edge; Top = $overlapTop; Bottom = $overlapBottom } }
    }
}
if (-not $pairs) { 'No two monitors sit side by side with enough shared height: the slide check is skipped.' }

foreach ($pair in $pairs) {
    $tabMon = $pair.Tab.B; $nextMon = $pair.Neighbour.B
    "Slide check: tab on $($pair.Tab.Id)'s $($pair.Edge) edge, neighbour $($pair.Neighbour.Id) beyond it."
    $midY = [int](($pair.Top + $pair.Bottom) / 2)
    # Doubles on purpose: [math]::Min(1, 0.63) picks the integer overload and returns 1.
    $ratio = [math]::Min(1.0, [math]::Max(0.0, [double]($midY - $tabMon.Top - 38) / [double]($tabMon.Height - 76)))
    $ratioText = $ratio.ToString([Globalization.CultureInfo]::InvariantCulture)

    # The strip of the neighbour next to the shared edge, where a clipped-away panel would show.
    $stripW = 340; $stripH = 320
    $stripX = if ($pair.Edge -eq 'left') { $nextMon.Right - $stripW } else { $nextMon.Left }
    $stripY = $midY - [int]($stripH / 2)
    $backdropRgb = 0x123456
    $backdrop = Start-Backdrop -X $stripX -Y $stripY -Width $stripW -Height $stripH -Seconds 40 -Color '123456'

    $bmp = New-Object System.Drawing.Bitmap($stripW, $stripH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    function Scan { $g.CopyFromScreen($stripX, $stripY, 0, 0, (New-Object System.Drawing.Size($stripW, $stripH))); [PixelScan]::CountDifferent($bmp, $backdropRgb) }

    $app2 = $null; $fake = $null
    try {
        # Control: a window of another colour over part of the strip must be seen, or a clean scan
        # below would prove nothing.
        $fake = Start-Backdrop -X ($stripX + 40) -Y ($stripY + 40) -Width 100 -Height 100 -Seconds 6 -Color 'FF8800'
        Check 'control: the scan sees a foreign window on the strip' ((Scan) -gt 5000)
        Stop-Process -Id $fake.Id -Force -ErrorAction SilentlyContinue; $fake = $null
        Start-Sleep -Milliseconds 400
        Check 'control: the strip is clean again without it' ((Scan) -eq 0)

        $app2 = Start-TestApp -Tabs @('QuickActions') -TabExtra @("verticalRatio=$ratioText`nedge=$($pair.Edge)`nmonitor=$($pair.Tab.Id)") `
                              -Settings @('theme=Dark', 'fullscreenGuard=0') -StartupSeconds 3
        $tab = @(Get-TestTabs $app2)[0]
        Check 'the test tab is on the expected monitor edge' ([bool]$tab -and $(if ($pair.Edge -eq 'left') { $tab.L -eq $tabMon.Left } else { $tab.R -eq $tabMon.Right }))
        Check 'the strip is clean with the tab present' ((Scan) -eq 0)

        $dirtyFrames = 0; $frames = 0
        for ($round = 1; $round -le 3; $round++) {
            # Off the tab first, so the move onto it is always a fresh hover.
            Move-Mouse ($tabMon.X + [int]($tabMon.Width / 2)) ($tabMon.Y + [int]($tabMon.Height / 2))
            Start-Sleep -Milliseconds 150
            Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2)
            $sw = [Diagnostics.Stopwatch]::StartNew()
            while ($sw.ElapsedMilliseconds -lt 700) { $frames++; if ((Scan) -gt 0) { $dirtyFrames++ } }
            $opened = [bool](Get-TestPanel $app2)
            Move-Mouse ($tabMon.X + [int]($tabMon.Width / 2)) ($tabMon.Y + [int]($tabMon.Height / 2))
            $sw.Restart()
            while ($sw.ElapsedMilliseconds -lt 1200) { $frames++; if ((Scan) -gt 0) { $dirtyFrames++ } }  # the slide out, too
            Check "round ${round}: the panel opened" $opened
        }
        "  scanned $frames frames of the neighbour's strip while the panel slid in and out"
        Check 'the panel never showed on the neighbouring monitor' ($dirtyFrames -eq 0) "$dirtyFrames of $frames frames had foreign pixels"
        Check 'enough frames were scanned to have caught a slide' ($frames -ge 40) "$frames frames"
    } finally {
        $g.Dispose(); $bmp.Dispose()
        if ($app2) { Stop-TestApp $app2 }
        foreach ($leftover in @($backdrop, $fake)) { if ($leftover) { Stop-Process -Id $leftover.Id -Force -ErrorAction SilentlyContinue } }
    }
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

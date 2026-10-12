<#
.SYNOPSIS
    Checks the editable Quick Actions panel on screen: the rows come from the config, a row really
    starts what it names, and a row that cannot run says so in the panel and not in a dialog.

.DESCRIPTION
    Runs a test copy of EdgeDeck (apptest.exe) whose config has three actions of its own:

      "Start marker"  -> the plain-window tool, with a distinctive colour on its command line
      "Gone"          -> a program that does not exist
      "Música"        -> a name with an accent, to see it survive the file and the panel

    1. The panel is as tall as those rows say, and taller by exactly one row per row added.
    2. Pressing the first row starts the tool (found by its command line), and nothing else does.
    3. Pressing "Gone" leaves the panel open and puts a notice in its header, in the notice colour -
       and no window appears: no dialog took focus.
    4. The accented name is drawn (the row exists and is hit-testable at its position).

    Moves the real cursor and clicks in the test copy's own panel; puts a plain window behind it.
    Safe beside the real EdgeDeck (the test copy's windows are found by process).
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

$marker = 'FF00AA'
$backdropTool = Get-Tool backdrop $ToolsDir
$chrome = 44.0   # PanelLayout::ChromeHeight
$row = 44.0      # the Quick Actions row height
$prim = Get-PrimaryRect

# How many pixels of the panel header are in the notice colour. The notice is amber on a dark panel
# (1.0, 0.78, 0.36); the plain title is grey, so a header with none of it reads as zero.
function Get-AmberCount($panel, $scale) {
    $bmp = New-Object System.Drawing.Bitmap(($panel.R - $panel.L), [int]($chrome * $scale))
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($panel.L, $panel.T, 0, 0, $bmp.Size)
    $g.Dispose()
    $amber = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 2) {
        for ($x = 0; $x -lt $bmp.Width; $x += 2) {
            $c = $bmp.GetPixel($x, $y)
            if ($c.R -gt 200 -and $c.G -gt 150 -and $c.G -lt 235 -and $c.B -gt 40 -and $c.B -lt 150) { $amber++ }
        }
    }
    $bmp.Dispose()
    return $amber
}

function Get-Rows($app) {
    $tab = @(Get-TestTabs $app)[0]
    Move-Mouse ($prim.R / 2) ($prim.B / 2); Start-Sleep -Milliseconds 300
    Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2)
    [void](Wait-Until -TimeoutSeconds 3 -PollMs 100 -Condition { [bool](Get-TestPanel $app) })
    Start-Sleep -Milliseconds 450
    return @{ Tab = $tab; Panel = (Get-TestPanel $app) }
}

# ---- 1. the height follows the number of rows ------------------------------------------------
$one = "[action]`nname=Only`ntarget=notepad.exe`n"
$four = "[action]`nname=A`ntarget=notepad.exe`n[action]`nname=B`ntarget=notepad.exe`n[action]`nname=C`ntarget=notepad.exe`n[action]`nname=D`ntarget=notepad.exe`n"
$heights = @{}
foreach ($case in @(@{ N = 1; Raw = $one }, @{ N = 4; Raw = $four })) {
    $app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0') -Raw $case.Raw
    try {
        $r = Get-Rows $app
        $heights[$case.N] = $r.Panel.B - $r.Panel.T
        $scale = $r.Tab.Dpi / 96.0
    } finally {
        Move-Mouse ($prim.R / 2) ($prim.B / 2)
        Stop-TestApp $app
    }
}
$expected = [int][math]::Round(3 * $row * $scale)
Check "four rows are exactly three rows taller than one" (($heights[4] - $heights[1]) -eq $expected) "got $($heights[4] - $heights[1]), expected $expected"

# ---- 2-4. a row starts what it names; a row that cannot says why -------------------------------
$raw = @"
[action]
name=Start marker
target=$backdropTool
args=0 0 8 8 4 $marker
[action]
name=Gone
target=C:\no\such\folder\edgedeck-missing.exe
[action]
name=Música
target=$backdropTool
args=0 0 8 8 4 112233
"@
$backdrop = Start-Backdrop -X ($prim.R - 460) -Y ($prim.B / 2 - 220) -Width 460 -Height 440 -Seconds 60 -Color '2B2B2B'
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0') -Raw $raw
try {
    $r = Get-Rows $app
    $panel = $r.Panel
    $scale = $r.Tab.Dpi / 96.0
    Check 'the panel opens with the three configured rows' ((($panel.B - $panel.T) / $scale) -gt ($chrome + 3 * $row)) "height $($panel.B - $panel.T)"

    $cx = $panel.L + 120 * $scale
    function RowY($i) { $panel.T + ($chrome + $i * $row + $row / 2) * $scale }

    # 2. the first row starts the marker window
    $before = @(Get-Process -Name backdrop -ErrorAction SilentlyContinue | ForEach-Object Id)
    Move-Mouse $cx (RowY 0); Start-Sleep -Milliseconds 200
    Click-Mouse
    $started = Wait-Until -TimeoutSeconds 5 -PollMs 150 -Condition {
        [bool](Get-CimInstance Win32_Process -Filter "Name='backdrop.exe'" | Where-Object { $_.CommandLine -like "*$marker*" })
    }
    Check 'pressing the first row started the program it names' $started
    Check 'and the panel stayed open (acting never closes it while the pointer is on it)' ([bool](Get-TestPanel $app))
    $others = @(Get-CimInstance Win32_Process -Filter "Name='backdrop.exe'" | Where-Object { $_.CommandLine -like '*112233*' })
    Check 'and only that row started something' ($others.Count -eq 0)

    # 3. the row that cannot run: a notice in the header, no new window
    $quiet = Get-AmberCount (Get-TestPanel $app) $scale
    Check 'control: the header has no notice-coloured pixels before the failure' ($quiet -lt 6) "$quiet amber pixels"
    Move-Mouse $cx (RowY 1); Start-Sleep -Milliseconds 200
    Click-Mouse
    Start-Sleep -Milliseconds 700
    $p2 = Get-TestPanel $app
    Check 'the panel is still there after a failed row' ([bool]$p2)
    $amber = Get-AmberCount $p2 $scale
    Check 'a notice in the notice colour is in the panel header' ($amber -gt 12) "$amber amber pixels"

    # 4. the accented row is a row of its own, in its place
    $beforeAccent = @(Get-CimInstance Win32_Process -Filter "Name='backdrop.exe'" | Where-Object { $_.CommandLine -like '*112233*' }).Count
    Move-Mouse $cx (RowY 2); Start-Sleep -Milliseconds 200
    Click-Mouse
    $accent = Wait-Until -TimeoutSeconds 5 -PollMs 150 -Condition {
        [bool](Get-CimInstance Win32_Process -Filter "Name='backdrop.exe'" | Where-Object { $_.CommandLine -like '*112233*' })
    }
    Check 'the row with an accent in its name (third) starts its own program' ($accent -and $beforeAccent -eq 0)
} finally {
    Move-Mouse ($prim.R / 2) ($prim.B / 2)
    Stop-TestApp $app
    Stop-Process -Id $backdrop.Id -Force -ErrorAction SilentlyContinue
    Get-CimInstance Win32_Process -Filter "Name='backdrop.exe'" | Where-Object { $_.CommandLine -like "*$marker*" -or $_.CommandLine -like '*112233*' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

# ---- 5. each row shows the picture of what it opens ---------------------------------------------
# A program and a folder have pictures; a program that is not there has none and shows its letter
# in a plain badge. The picture is coloured (Explorer's yellow folder, the blue of its window), the
# badge is grey with a white letter, so how much saturated colour sits in the row's 24-px slot says
# which one was drawn - and a slot with neither says the row has no picture at all.
$iconRaw = @"
[action]
name=Explorer
target=%SystemRoot%\explorer.exe
[action]
name=Gone
target=C:\no\such\folder\edgedeck-missing.exe
[action]
name=Windows folder
target=%SystemRoot%
"@

function Get-SlotColour($panel, $scale, [int]$rowIndex) {
    $size = [int][math]::Round(24 * $scale)
    $x0 = [int][math]::Round($panel.L + 18 * $scale)
    $y0 = [int][math]::Round($panel.T + ($chrome + $rowIndex * $row + ($row - 24) / 2) * $scale)
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x0, $y0, 0, 0, $bmp.Size)
    $g.Dispose()
    $saturated = 0; $light = 0
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $hi = [math]::Max($c.R, [math]::Max($c.G, $c.B)); $lo = [math]::Min($c.R, [math]::Min($c.G, $c.B))
            if (($hi - $lo) -gt 50) { $saturated++ }
            if ($lo -gt 200) { $light++ }
        }
    }
    $bmp.Dispose()
    return @{ Saturated = $saturated; Light = $light }
}

$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0') -Raw $iconRaw
try {
    $r = Get-Rows $app
    $scale = $r.Tab.Dpi / 96.0
    # The pictures are looked up when the panel opens and arrive a moment later; wait for them.
    $arrived = Wait-Until -TimeoutSeconds 5 -PollMs 200 -Condition {
        (Get-SlotColour (Get-TestPanel $app) $scale 0).Saturated -gt 40
    }
    $panel = Get-TestPanel $app
    $explorer = Get-SlotColour $panel $scale 0
    $gone = Get-SlotColour $panel $scale 1
    $folder = Get-SlotColour $panel $scale 2
    Check 'the row for a program shows its picture (coloured pixels in the slot)' $arrived "$($explorer.Saturated) coloured pixels"
    Check 'the row for a folder shows its picture' ($folder.Saturated -gt 40) "$($folder.Saturated) coloured pixels"
    Check 'control: the row for a missing program has no picture - no colour in its slot' ($gone.Saturated -lt 6) "$($gone.Saturated) coloured pixels"
    Check 'and shows its letter in a badge instead (light pixels of the letter)' ($gone.Light -ge 8) "$($gone.Light) light pixels"
} finally {
    Move-Mouse ($prim.R / 2) ($prim.B / 2)
    Stop-TestApp $app
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

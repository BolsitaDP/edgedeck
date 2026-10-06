<#
.SYNOPSIS
    Saves one picture with every widget's tab icon, enlarged, for judging icons by eye.

.DESCRIPTION
    Starts a test copy of EdgeDeck (apptest.exe, scratch config) with one tab per widget type,
    puts a plain strip behind the right edge of the primary monitor, and stitches a zoomed
    crop of each tab into a single PNG. Only the test copy's tabs are used (picked by process),
    and the strip behind them means the picture holds nothing from the desktop.

    The scale and the icons come from the real renderer, so a change to TabIcon, to the icon
    font lookup or to the tab size shows up here exactly as it will on screen.

.PARAMETER Path
    Where to write the PNG. Default: tab-icons.png in the temp folder.
.PARAMETER Zoom
    Magnification of each tab (nearest-neighbour, so pixels stay visible).
#>
[CmdletBinding()]
param([string]$Path = (Join-Path ([IO.Path]::GetTempPath()) 'tab-icons.png'), [int]$Zoom = 4, [string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"
Add-Type -AssemblyName System.Drawing

$widgets = 'QuickActions', 'Media', 'Volume', 'Brightness', 'Lyrics', 'Displays'
$primary = Get-PrimaryRect
$backdrop = Start-Backdrop -X ($primary.R - 60) -Y 0 -Width 60 -Height $primary.B -Seconds 60 -Color '2B2B2B' -ToolsDir $ToolsDir
$app = Start-TestApp -Tabs $widgets
try {
    $tabs = Get-TestTabs $app
    if ($tabs.Count -ne $widgets.Count) { throw "Expected $($widgets.Count) tabs, found $($tabs.Count)." }

    $cellW = 44; $cellH = 92; $gap = 10
    $sheet = New-Object System.Drawing.Bitmap(($cellW * $Zoom * $widgets.Count + $gap * ($widgets.Count + 1)), ($cellH * $Zoom + 2 * $gap))
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.Clear([System.Drawing.Color]::FromArgb(43, 43, 43)); $g.InterpolationMode = 'NearestNeighbor'; $g.PixelOffsetMode = 'Half'
    for ($i = 0; $i -lt $tabs.Count; $i++) {
        $crop = Join-Path ([IO.Path]::GetTempPath()) "tab-$i.png"
        Take-Shot ($tabs[$i].L - 9) ($tabs[$i].T - 8) $cellW $cellH $crop
        $image = [System.Drawing.Image]::FromFile($crop)
        $g.DrawImage($image, ($gap + $i * ($cellW * $Zoom + $gap)), $gap, ($cellW * $Zoom), ($cellH * $Zoom))
        $image.Dispose(); Remove-Item $crop -Force
    }
    $g.Dispose(); $sheet.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png); $sheet.Dispose()
    Write-Host "Saved $Path  (left to right: $($widgets -join ', '))"
} finally {
    Stop-TestApp $app
    Stop-Process -Id $backdrop.Id -Force -ErrorAction SilentlyContinue
}

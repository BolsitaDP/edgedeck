<#
.SYNOPSIS
    Makes the README's picture of the panels: docs\images\panels-<dark|light>-<en|es>.png.

.DESCRIPTION
    Draws every panel with panelshot on a desktop of its own (deskrun), so nothing appears on
    screen and it can run while you work, and lays them out in three columns over GitHub's page
    colour for each theme.

    The panels show THIS MACHINE'S data: what is playing, the audio devices, the monitors' names.
    Look at the pictures before committing them. Recent files is pointed at a folder of made-up
    files instead of your Downloads; Quick Actions shows the first-start list (panelshot reads no
    one's settings). Lyrics needs LRCLIB to be reachable and something playing that it knows.

.PARAMETER OutDir
    Where to write the PNGs. Default: docs\images in this repository.
.PARAMETER Language
    en, es, or both (the default).
#>
[CmdletBinding()]
param([string]$OutDir, [ValidateSet('en', 'es', 'both')][string]$Language = 'both', [string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"
Add-Type -AssemblyName System.Drawing

if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\..\docs\images' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$deskrun = Get-Tool deskrun $ToolsDir
$panelshot = Get-Tool panelshot $ToolsDir

# Made-up recent files, each as old as its minutes say.
$scratch = Join-Path ([IO.Path]::GetTempPath()) "edgedeck-readme-$PID"
$recent = Join-Path $scratch 'recent'
New-Item -ItemType Directory -Force $recent | Out-Null
$samples = [ordered]@{
    'Quarterly report.pdf' = 3; 'holiday-photos.zip' = 25; 'Invoice 2026-10.xlsx' = 70
    'setup-notes.txt' = 180; 'Presentation draft.pptx' = 1500
}
foreach ($name in $samples.Keys) {
    $file = Join-Path $recent $name
    Set-Content -Path $file -Value 'sample'
    $when = (Get-Date).AddMinutes(-$samples[$name])
    (Get-Item $file).CreationTime = $when
    (Get-Item $file).LastWriteTime = $when
}

# Columns of panels, top to bottom; the background is GitHub's page colour in each theme.
$columns = @(@('QuickActions', 'Media', 'Volume'), @('Lyrics', 'Brightness'), @('Recent', 'Displays'))
$themes = [ordered]@{ dark = '0D1117'; light = 'FFFFFF' }
$languages = if ($Language -eq 'both') { @('en', 'es') } else { @($Language) }
$gap = 16; $pad = 16

try {
    foreach ($lang in $languages) {
        foreach ($theme in $themes.Keys) {
            $bg = $themes[$theme]
            $images = @{}
            foreach ($widget in ($columns | ForEach-Object { $_ })) {
                $bmp = Join-Path $scratch "$widget-$theme-$lang.bmp"
                $arguments = @($widget, $theme, $bmp, "bg=$bg", 'wait=3000', "lang=$lang")
                if ($widget -eq 'Recent') { $arguments += "recent=$recent" }
                & $deskrun $panelshot @arguments | Out-Null
                if (-not (Test-Path $bmp)) { throw "panelshot drew nothing for $widget ($theme, $lang)." }
                $images[$widget] = [System.Drawing.Image]::FromFile($bmp)
            }

            $widths = @($columns | ForEach-Object { ($_ | ForEach-Object { $images[$_].Width } | Measure-Object -Maximum).Maximum })
            $heights = @($columns | ForEach-Object { $h = -$gap; foreach ($w in $_) { $h += $images[$w].Height + $gap }; $h })
            $width = [int](($widths | Measure-Object -Sum).Sum + $gap * ($columns.Count - 1) + 2 * $pad)
            $height = [int](($heights | Measure-Object -Maximum).Maximum + 2 * $pad)

            $canvas = New-Object System.Drawing.Bitmap -ArgumentList $width, $height
            $g = [System.Drawing.Graphics]::FromImage($canvas)
            try {
                $g.Clear([System.Drawing.ColorTranslator]::FromHtml("#$bg"))
                $x = $pad
                for ($c = 0; $c -lt $columns.Count; $c++) {
                    $y = $pad
                    foreach ($widget in $columns[$c]) {
                        $image = $images[$widget]
                        $g.DrawImage($image, $x, $y, $image.Width, $image.Height)
                        $y += $image.Height + $gap
                    }
                    $x += $widths[$c] + $gap
                }
                $png = Join-Path $OutDir "panels-$theme-$lang.png"
                $canvas.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
                "$png  ${width}x$height"
            } finally {
                $g.Dispose(); $canvas.Dispose()
                $images.Values | ForEach-Object { $_.Dispose() }
            }
        }
    }
} finally {
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
}

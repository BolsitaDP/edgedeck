<#
.SYNOPSIS
    Draws EdgeDeck's application icon and writes res\EdgeDeck.ico.

.DESCRIPTION
    The icon is the app in one picture: a panel and, five units to its right and flush with the
    edge, the tab that opens it. It is a blue rounded square with white shapes, so it holds up
    on a dark taskbar and on a light one, which the first icon (a dark square with a thin blue
    sliver) did not: on the dark notification area it was close to invisible.

    Every size is drawn from the same vector description at 4x and scaled down, with the
    detail (the rows inside the panel) left out below 32 px where it would only be noise.
    Sizes: 16, 20, 24, 32, 40, 48, 64, 128, 256 - the ones Windows asks for at 100-250 % scaling
    (16/20/24/32/40 for the notification area and title bars, 48 and up for Explorer).

    Writes PNG-compressed entries, which Windows reads from Vista on. Re-run this to change the
    picture; the .ico is a build input, not something generated at build time, so that building
    needs nothing but the compiler.

.PARAMETER Path
    Where to write the icon. Default: res\EdgeDeck.ico in the repository.

.PARAMETER Preview
    Also save a contact sheet of the sizes to this PNG, for looking at the result.
#>
[CmdletBinding()]
param(
    [string]$Path,
    [string]$Preview
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
# Computed here, not as the parameter's default: $PSScriptRoot is not set yet while the defaults are.
if (-not $Path) { $Path = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'res\EdgeDeck.ico' }

function New-RoundedRect([double]$x, [double]$y, [double]$w, [double]$h, [double]$r) {
    $d = $r * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-IconBitmap([int]$size) {
    $scale = 4
    $big = $size * $scale
    $canvas = New-Object System.Drawing.Bitmap($big, $big, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($canvas)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $k = $big / 64.0   # the drawing is described on a 64 x 64 grid

    # The plate. The corner radius is smaller on the small sizes, where a big one eats the shapes.
    $radius = if ($size -ge 48) { 14 } elseif ($size -ge 24) { 12 } else { 10 }
    $plate = New-RoundedRect 0 0 $big $big ($radius * $k)
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Rectangle(0, 0, $big, $big)),
        [System.Drawing.Color]::FromArgb(255, 0x5B, 0xAE, 0xFF),
        [System.Drawing.Color]::FromArgb(255, 0x1F, 0x5C, 0xE3), 50.0)
    $g.FillPath($gradient, $plate)

    $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))

    # The panel, and the tab that opens it, with the same gap between them as in the app.
    $panel = New-RoundedRect (8 * $k) (14 * $k) (34 * $k) (36 * $k) (6 * $k)
    $g.FillPath($white, $panel)
    $tab = New-RoundedRect (47 * $k) (23 * $k) (9 * $k) (18 * $k) (4.5 * $k)
    $g.FillPath($white, $tab)

    # Rows inside the panel, so it reads as a list and not as a blank white box (which at 16 px
    # looks like a battery). Three thin rows where there is room; below that two thick ones, as
    # thick as a pixel and a half, which is as much as 16 px can carry.
    $rowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(240, 0x2A, 0x6A, 0xE8))
    $rows = if ($size -ge 32) { @(@(22, 4), @(31, 4), @(40, 4)) } else { @(@(23.5, 6), @(34.5, 6)) }
    foreach ($row in $rows) {
        $path = New-RoundedRect (14 * $k) ($row[0] * $k) (22 * $k) ($row[1] * $k) ([Math]::Min($row[1] / 2, 2) * $k)
        $g.FillPath($rowBrush, $path)
        $path.Dispose()
    }
    $rowBrush.Dispose()

    $g.Dispose(); $gradient.Dispose(); $white.Dispose(); $plate.Dispose(); $panel.Dispose(); $tab.Dispose()

    $out = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $og = [System.Drawing.Graphics]::FromImage($out)
    $og.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $og.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $og.DrawImage($canvas, 0, 0, $size, $size)
    $og.Dispose(); $canvas.Dispose()
    return $out
}

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = foreach ($size in $sizes) {
    $bmp = New-IconBitmap $size
    $stream = New-Object System.IO.MemoryStream
    $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    [pscustomobject]@{ Size = $size; Bitmap = $bmp; Png = $stream.ToArray() }
    $stream.Dispose()
}

# ICONDIR, one ICONDIRENTRY per image, then the images.
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dim = if ($image.Size -ge 256) { 0 } else { $image.Size }   # 0 means 256 in the directory
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$image.Png.Length); $bw.Write([uint32]$offset)
    $offset += $image.Png.Length
}
foreach ($image in $images) { $bw.Write($image.Png) }
$bw.Flush()
[IO.File]::WriteAllBytes($Path, $ms.ToArray())
$bw.Dispose(); $ms.Dispose()
"Wrote $Path ($((Get-Item $Path).Length) bytes, $($images.Count) sizes: $($sizes -join ', '))"

if ($Preview) {
    # The picture on a dark and on a light background, at 1x and enlarged, as the taskbar would show it.
    $sheet = New-Object System.Drawing.Bitmap(1000, 330)
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    foreach ($band in @(@{ Y = 0; Color = [System.Drawing.Color]::FromArgb(0x20, 0x20, 0x20) },
                        @{ Y = 165; Color = [System.Drawing.Color]::FromArgb(0xF3, 0xF3, 0xF3) })) {
        $g.FillRectangle((New-Object System.Drawing.SolidBrush($band.Color)), 0, $band.Y, 1000, 165)
        $x = 10
        foreach ($image in $images) {
            if ($image.Size -gt 128) { continue }
            $zoom = if ($image.Size -le 24) { 4 } elseif ($image.Size -le 48) { 2 } else { 1 }
            $draw = $image.Size * $zoom
            $g.DrawImage($image.Bitmap, $x, $band.Y + 10, $draw, $draw)
            $x += $draw + 14
        }
    }
    $g.Dispose()
    $sheet.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    "Preview: $Preview"
}
foreach ($image in $images) { $image.Bitmap.Dispose() }

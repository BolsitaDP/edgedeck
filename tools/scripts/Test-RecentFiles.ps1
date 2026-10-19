<#
.SYNOPSIS
    Checks the Recent files panel on screen: it lists the newest files of the configured folder,
    lists them again at every opening, draws a picture or a badge for each, and a click opens the
    file.

.DESCRIPTION
    Runs a test copy of EdgeDeck (apptest.exe) with one Recent files tab pointed at a scratch folder:

      marker.lnk       newest; a shortcut to the plain-window tool with a distinctive colour on
                       its command line, which is how a click is seen
      a program        a copy of WerFault.exe: carries an icon of its own, drawn as a picture, and
                       does nothing visible if it is ever started by mistake
      beta.zip         no icon of its own: drawn as a badge ("ZIP")
      alpha.pdf        oldest
      hidden / partial download / sub-folder: must not be listed

    1. The panel is as tall as those rows say (and a "folder" row), and one file more is exactly
       one row taller the next time it is opened - the folder is listed at every opening.
    2. A program's row shows a picture and a plain file's a badge: a badge is one flat colour
       over most of its square, a picture is not.
    3. Pressing a file's row starts what opens it, and leaves the panel open.
    4. A file nothing is set to open leaves a notice in the panel header, in the notice colour,
       and no dialog.
    5. A folder that is not there, and an empty one, give the short panels they should.

    Moves the real cursor and clicks in the test copy's own panel. Safe beside the real EdgeDeck
    (the test copy's windows are found by process).
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

$chrome = 44.0     # PanelLayout::ChromeHeight
$bottom = 10.0     # PanelLayout::BottomPadding
$row = 48.0        # one file's row
$folderRow = 40.0  # the "open folder" row
$message = 44.0    # what stands in for the rows when there are none
$prim = Get-PrimaryRect

$work = Join-Path ([IO.Path]::GetTempPath()) "edgedeck-recent-$PID"
New-Item -ItemType Directory -Force -Path $work | Out-Null

function New-File([string]$name, [int]$minutesAgo, [string]$content = 'x', [switch]$Hidden) {
    $path = Join-Path $work $name
    Set-Content -LiteralPath $path -Value $content
    (Get-Item -LiteralPath $path).LastWriteTime = (Get-Date).AddMinutes(-$minutesAgo)
    if ($Hidden) { (Get-Item -LiteralPath $path -Force).Attributes = 'Hidden' }
    return $path
}

function Get-Panel($app, [switch]$Fresh) {
    $tab = @(Get-TestTabs $app)[0]
    # Leave, and wait until it has really closed: a panel that never closed is not opened again,
    # and so is not listed again - which is what this is meant to see.
    Move-Mouse ($prim.R / 2) ($prim.B / 2)
    [void](Wait-Until -TimeoutSeconds 4 -PollMs 100 -Condition { -not (Get-TestPanel $app) })
    Start-Sleep -Milliseconds 300
    Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2)
    [void](Wait-Until -TimeoutSeconds 3 -PollMs 100 -Condition { [bool](Get-TestPanel $app) })
    Start-Sleep -Milliseconds 900   # the listing arrives a moment after the panel opens
    return @{ Tab = $tab; Panel = (Get-TestPanel $app); Scale = ($tab.Dpi / 96.0) }
}

function Get-Height($p) { $p.Panel.B - $p.Panel.T }

# How much of a row's picture slot is one flat colour: a badge is (a rounded square of one colour
# with a short word on it), a picture - a program's icon - is not.
function Get-FlatFraction($panel, $scale, [int]$rowIndex) {
    $size = [int][math]::Round(28 * $scale)
    $x0 = [int][math]::Round($panel.L + 18 * $scale)
    $y0 = [int][math]::Round($panel.T + ($chrome + $rowIndex * $row + ($row - 28) / 2) * $scale)
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x0, $y0, 0, 0, $bmp.Size)
    $g.Dispose()
    $buckets = @{}
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $key = '{0}.{1}.{2}' -f ($c.R -shr 4), ($c.G -shr 4), ($c.B -shr 4)
            $buckets[$key] = 1 + [int]$buckets[$key]
        }
    }
    $bmp.Dispose()
    $top = ($buckets.Values | Measure-Object -Maximum).Maximum
    return [double]$top / ($size * $size)
}

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

$marker = 'FF00AB'
$backdropTool = Get-Tool backdrop $ToolsDir
function New-Shortcut([string]$name, [string]$target, [string]$arguments, [int]$minutesAgo) {
    $path = Join-Path $work $name
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($path)
    $link.TargetPath = $target
    $link.Arguments = $arguments
    $link.Save()
    (Get-Item -LiteralPath $path).LastWriteTime = (Get-Date).AddMinutes(-$minutesAgo)
    return $path
}
try {
    # ---- the folder ------------------------------------------------------------------------
    [void](New-Shortcut 'edgedeck-recent-marker.lnk' $backdropTool "0 0 8 8 6 $marker" 0)
    $program = Join-Path $work 'gamma.exe'
    Copy-Item -LiteralPath "$env:SystemRoot\System32\WerFault.exe" -Destination $program
    (Get-Item -LiteralPath $program).LastWriteTime = (Get-Date).AddMinutes(-1)
    [void](New-File 'beta.zip' 5)
    [void](New-File 'alpha.pdf' 60)
    [void](New-File 'secret.txt' 0 'x' -Hidden)
    [void](New-File 'download.zip.crdownload' 0)
    New-Item -ItemType Directory -Force -Path (Join-Path $work 'a sub-folder') | Out-Null

    $app = Start-TestApp -Tabs Recent -Settings @('theme=Dark', 'fullscreenGuard=0', "recentFolder=$work")
    try {
        # ---- 1. height, and the listing is redone at every opening ------------------------------
        $p = Get-Panel $app
        $scale = $p.Scale
        $expected4 = ($chrome + 4 * $row + $folderRow + $bottom) * $scale
        Check 'the panel is as tall as four files and the folder row say (hidden, partial and folders left out)' `
            ([math]::Abs((Get-Height $p) - $expected4) -le 2) "height $(Get-Height $p), expected $([int]$expected4)"

        [void](New-File 'zeta.docx' 0)
        $p = Get-Panel $app
        Check 'a file added since is listed the next time the panel opens: exactly one row taller' `
            ([math]::Abs((Get-Height $p) - ($expected4 + $row * $scale)) -le 2) "height $(Get-Height $p)"

        # ---- 2. a picture for a program, a badge for the rest -----------------------------------
        # Newest first now: zeta.docx, marker.lnk, gamma.exe (the program), beta.zip, alpha.pdf.
        $program = Get-FlatFraction $p.Panel $scale 2
        $zip = Get-FlatFraction $p.Panel $scale 3
        $pdf = Get-FlatFraction $p.Panel $scale 4
        Check 'a plain file is badged: one flat colour fills most of its square' ($zip -gt 0.6 -and $pdf -gt 0.6) ("zip {0:N2}, pdf {1:N2}" -f $zip, $pdf)
        Check 'a program shows its own picture, not a badge' ($program -lt 0.5) ("{0:N2} flat" -f $program)

        # ---- 3. a click opens the file ----------------------------------------------------------
        $cx = $p.Panel.L + 120 * $scale
        function RowY($i) { $p.Panel.T + ($chrome + $i * $row + $row / 2) * $scale }
        Move-Mouse $cx (RowY 1); Start-Sleep -Milliseconds 250   # marker.lnk is the second row
        Click-Mouse
        $started = Wait-Until -TimeoutSeconds 6 -PollMs 150 -Condition {
            [bool](Get-CimInstance Win32_Process -Filter "Name='backdrop.exe'" | Where-Object { $_.CommandLine -like "*$marker*" })
        }
        Check 'pressing a file opens it (started by its own name)' $started
        Check 'and the panel stays open' ([bool](Get-TestPanel $app))

        # ---- 4. a file nothing opens: a notice in the header, no dialog -------------------------
        $quiet = Get-AmberCount (Get-TestPanel $app) $scale
        [void](New-File 'unknown-kind.edgedecktest' 0)
        $p = Get-Panel $app    # listed again: it is the newest, so the first row
        Move-Mouse $cx (RowY 0); Start-Sleep -Milliseconds 250
        Click-Mouse
        Start-Sleep -Milliseconds 800
        $amber = Get-AmberCount (Get-TestPanel $app) $scale
        Check 'control: no notice-coloured pixels in the header before' ($quiet -lt 6) "$quiet"
        Check 'a file that nothing is set to open leaves a notice in the header' ($amber -gt 12) "$amber amber pixels"
        Check 'and no Open with window appears' (-not (Get-Process -Name OpenWith -ErrorAction SilentlyContinue)) 'an OpenWith process is running'
    } finally {
        Move-Mouse ($prim.R / 2) ($prim.B / 2)
        Stop-TestApp $app
        Get-CimInstance Win32_Process -Filter "Name='backdrop.exe'" | Where-Object { $_.CommandLine -like "*$marker*" } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    }

    # ---- 5. a folder that is not there, and an empty one ---------------------------------------
    $missing = Join-Path $work 'no-such-folder'
    $app = Start-TestApp -Tabs Recent -Settings @('theme=Dark', 'fullscreenGuard=0', "recentFolder=$missing")
    try {
        $p = Get-Panel $app
        Check 'a folder that is not there is one short message and no folder row' `
            ([math]::Abs((Get-Height $p) - ($chrome + $message + $bottom) * $p.Scale) -le 2) "height $(Get-Height $p)"
    } finally {
        Move-Mouse ($prim.R / 2) ($prim.B / 2)
        Stop-TestApp $app
    }

    $empty = Join-Path $work 'empty'
    New-Item -ItemType Directory -Force -Path $empty | Out-Null
    $app = Start-TestApp -Tabs Recent -Settings @('theme=Dark', 'fullscreenGuard=0', "recentFolder=$empty")
    try {
        $p = Get-Panel $app
        Check 'an empty folder is the message and the folder row' `
            ([math]::Abs((Get-Height $p) - ($chrome + $message + $folderRow + $bottom) * $p.Scale) -le 2) "height $(Get-Height $p)"
    } finally {
        Move-Mouse ($prim.R / 2) ($prim.B / 2)
        Stop-TestApp $app
    }
} finally {
    Get-ChildItem -LiteralPath $work -Force -Recurse -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | ForEach-Object {
        if ($_.PSIsContainer) { [IO.Directory]::Delete($_.FullName, $false) } else { [IO.File]::Delete($_.FullName) }
    }
    [IO.Directory]::Delete($work, $false)
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

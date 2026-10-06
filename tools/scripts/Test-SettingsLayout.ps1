<#
.SYNOPSIS
    Checks the Settings window's layout at several display scales, without changing any
    display setting.

.DESCRIPTION
    Runs settingsharness.exe, which opens only the Settings window and, for each scale, sends
    it the WM_DPICHANGED a monitor at that scale would. The harness then reports controls
    outside the client area, overlapping controls, and labels, buttons or footer lines wider
    (or taller) than the control that holds them. Any finding fails the script.

    Safe to run at any time: it opens one window for a couple of seconds and touches nothing
    else. It does not need the real EdgeDeck to be closed.

.PARAMETER Dpis
    Scales to check, as DPI (96 = 100%, 120 = 125%, 144 = 150%, 192 = 200%), separated by
    commas or spaces. A string rather than an array on purpose: `powershell -File` hands a
    comma list to an array parameter as one piece of text and joins its digits.
.PARAMETER ScreenshotDir
    Also save a picture of the window at each scale into this folder. The window is topmost
    and cropped to its visible bounds, so it holds nothing from the desktop.
#>
[CmdletBinding()]
param(
    [string]$Dpis = '96,120,144,168,192,240',
    [ValidateSet('dark', 'light', 'follow')][string]$Theme = 'dark',
    [string]$ToolsDir,
    [string]$ScreenshotDir
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

$harness = Get-Tool settingsharness $ToolsDir
if ($ScreenshotDir) { New-Item -ItemType Directory -Force -Path $ScreenshotDir | Out-Null }

$failed = $false
$scales = @($Dpis -split '[,\s]+' | Where-Object { $_ } | ForEach-Object { [int]$_ })
foreach ($dpi in $scales) {
    $out = [IO.Path]::GetTempFileName()
    $process = Start-Process -FilePath $harness -ArgumentList $Theme, 4, "dpi=$dpi" -PassThru -RedirectStandardOutput $out
    if ($ScreenshotDir) {
        Start-Sleep -Milliseconds 1800
        $window = @(Get-Wins 'EdgeDeckSettingsWindow' -ProcessId $process.Id)[0]
        if ($window) {
            $v = Get-VisibleRect $window.Hwnd
            Take-Shot $v.L $v.T $v.Width $v.Height (Join-Path $ScreenshotDir "settings-$dpi.png")
        }
    }
    if (-not $process.WaitForExit(15000)) { Stop-Process -Id $process.Id -Force }
    Start-Sleep -Milliseconds 200
    $lines = @(Get-Content $out)
    Remove-Item $out -Force

    $problems = 0
    foreach ($line in $lines) { if ($line -match '-> (\d+) problem') { $problems += [int]$Matches[1] } }
    $status = if ($problems -eq 0) { 'ok' } else { 'FAIL' }
    '{0,4} DPI ({1,3}%): {2}{3}' -f $dpi, [int]($dpi / 0.96), $status, $(if ($problems) { " - $problems problem(s)" } else { '' })
    if ($problems) {
        $failed = $true
        $lines | Where-Object { $_ -match 'TOO |OVERLAP|OUTSIDE' } | ForEach-Object { "      $($_.Trim())" }
    }
}
if ($failed) { exit 1 }

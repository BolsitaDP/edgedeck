<#
.SYNOPSIS
    Round-trips the monitor layouts with dispcfg.exe (the same code the Displays tab uses) and
    checks each switch took effect.

.DESCRIPTION
    THIS SWITCHES YOUR MONITORS. The screens blank for a few seconds at each step, windows
    move, and a layout change is saved to Windows' display database. It refuses to run unless
    you pass -AllowMonitorSwitching.

    By default it takes the lowest-impact route: it only turns the TV on and off (main <-> all),
    so the monitors you work on stay on. -IncludeTvOnly also goes through "TV only", which
    blanks them. It starts by saving a snapshot and always finishes by putting the starting
    layout back; if that does not read back, it restores the snapshot.

    Needs a TV to exist: with the default "largest panel" detection and no second, larger
    monitor to tell apart, the "main" and "all" layouts are not both available and the script
    says so and stops. Use -TvId to name the TV by its EDID id (see `dispcfg list`).
#>
[CmdletBinding()]
param(
    [switch]$AllowMonitorSwitching,
    [switch]$IncludeTvOnly,
    [string]$TvId,
    [string]$ToolsDir
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

if (-not $AllowMonitorSwitching) {
    Write-Host 'This script switches your monitors on and off. Re-run with -AllowMonitorSwitching to go ahead.'
    exit 2
}

$dispcfg = Get-Tool dispcfg $ToolsDir
$state = Get-DisplayState $ToolsDir
foreach ($p in 'main', 'all') {
    if (-not $state[$p].Available) { Write-Host "The '$p' layout is not available with what is connected; nothing to test."; exit 2 }
}
$start = ('main', 'all', 'tv' | Where-Object { $state[$_].Active } | Select-Object -First 1)
if (-not $start -or $start -eq 'tv') { Write-Host "Start from 'main' or 'all' (now: $(if ($start) { $start } else { 'a custom layout' }))."; exit 2 }

$snapshot = Join-Path ([IO.Path]::GetTempPath()) 'edgedeck-layout.snapshot'
& $dispcfg snap $snapshot | Out-Null

$other = if ($start -eq 'main') { 'all' } else { 'main' }
$steps = @($other, $start)
if ($IncludeTvOnly -and $state['tv'].Available) { $steps = @($other, 'tv', $start) }

$failures = 0
Write-Host "Starting layout: $start. Steps: $($steps -join ' -> ')"
foreach ($profile in $steps) {
    & $dispcfg apply $profile | Out-Null
    $ok = Wait-DisplayProfile $profile -ToolsDir $ToolsDir
    '{0} -> {1}' -f $profile, $(if ($ok) { 'PASS' } else { 'FAIL' })
    if (-not $ok) { $failures++ }
}

if (-not (Get-DisplayState $ToolsDir)[$start].Active) {
    Write-Warning "The starting layout did not come back; restoring the snapshot."
    & $dispcfg restore $snapshot
}
Remove-Item $snapshot -Force -ErrorAction SilentlyContinue
if ($failures) { Write-Host "$failures step(s) failed."; exit 1 }
Write-Host "All steps passed; layout is back to '$start'."

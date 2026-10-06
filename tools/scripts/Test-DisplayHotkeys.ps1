<#
.SYNOPSIS
    Presses the Displays tab's global shortcuts (Ctrl+Alt+Shift+1/2/3) for real, with the
    panel never opened, and checks the monitors follow.

.DESCRIPTION
    THIS SWITCHES YOUR MONITORS and presses real keys. It refuses to run without
    -AllowMonitorSwitching, and it needs the real EdgeDeck closed, because the real one owns
    those shortcuts: pass -StopRunningInstance to have the script close it (scripts\install.ps1
    starts it again afterwards).

    It starts a test copy with a Displays tab and takes the lowest-impact route, only turning
    the TV on and off:

      from "main":  Ctrl+Alt+Shift+3 (all)  -> TV on;   Ctrl+Alt+Shift+1 (main) -> TV off
      from "all":   Ctrl+Alt+Shift+1 (main) -> TV off;  Ctrl+Alt+Shift+3 (all)  -> TV on
      then the current layout's shortcut once more, which must change nothing.

    The panel is never opened, which is the point: a shortcut has to read the monitors itself
    because the panel's own state may be hours old. The layout it started in is restored at
    the end.
#>
[CmdletBinding()]
param([switch]$AllowMonitorSwitching, [switch]$StopRunningInstance, [string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

if (-not $AllowMonitorSwitching) {
    Write-Host 'This script switches your monitors and presses real keys. Re-run with -AllowMonitorSwitching to go ahead.'
    exit 2
}
Assert-NoRealInstance -StopIt:$StopRunningInstance

$dispcfg = Get-Tool dispcfg $ToolsDir
$state = Get-DisplayState $ToolsDir
foreach ($p in 'main', 'all') {
    if (-not $state[$p].Available) { Write-Host "The '$p' layout is not available with what is connected; nothing to test."; exit 2 }
}
$start = ('main', 'all' | Where-Object { $state[$_].Active } | Select-Object -First 1)
if (-not $start) { Write-Host "Start from the 'main' or 'all' layout."; exit 2 }

$keys = @{ main = 0x31; tv = 0x32; all = 0x33 }   # '1', '2', '3'
$other = if ($start -eq 'main') { 'all' } else { 'main' }
$failures = 0
function Check($label, $ok) { '{0} {1}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label; if (-not $ok) { $script:failures++ } }

$app = Start-TestApp -Tabs Displays
try {
    $registered = -not (Select-String -Path $app.Log -Pattern 'Ctrl\+Alt\+Shift' -Quiet)
    Check 'the shortcuts registered (nothing else owns them)' $registered

    Press-Chord $keys[$other]
    Check "Ctrl+Alt+Shift+$([char]$keys[$other]) -> $other (panel never opened)" (Wait-DisplayProfile $other -ToolsDir $ToolsDir)

    Press-Chord $keys[$start]
    Check "Ctrl+Alt+Shift+$([char]$keys[$start]) -> $start (panel never opened)" (Wait-DisplayProfile $start -ToolsDir $ToolsDir)

    Press-Chord $keys[$start]
    Start-Sleep -Seconds 3
    Check 'repeating the current layout changes nothing' ((Get-DisplayState $ToolsDir)[$start].Active)
    Check 'the log holds no error' (-not (Select-String -Path $app.Log -Pattern ' error ' -Quiet))
} finally {
    Stop-TestApp $app
    if (-not (Get-DisplayState $ToolsDir)[$start].Active) { & $dispcfg apply $start | Out-Null }
}
if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host "All checks passed; layout is back to '$start'."

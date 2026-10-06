<#
.SYNOPSIS
    Compares the resource cost of two EdgeDeck builds: idle CPU, CPU while hovering, memory,
    threads and handles. The way "is this change cheaper?" was answered for the per-pixel-alpha
    work (memory 34.5 -> 19.6 MB, private bytes 42 -> 5 MB, threads 24 -> 13).

.DESCRIPTION
    Runs each build alternately (A, B, A, B, ...) so slow drift on the machine hits both
    equally. Each run: start the real EdgeDeck.exe against a scratch config with two tabs,
    let it settle, measure CPU over an idle stretch, then hover the tab on and off with the
    real cursor and measure CPU again, then read memory, threads and handles.

    Moves the real cursor for about 20 seconds per run. The real EdgeDeck holds the
    single-instance lock, so it must be closed: pass -StopRunningInstance to have the script
    close it (scripts\install.ps1 starts it again afterwards).

    Results are noisy at this scale (tens of milliseconds); judge the memory columns and the
    trend over several runs, not one number.

.PARAMETER ExeA / ExeB
    The two EdgeDeck.exe files to compare. Typical: the previous build copied aside, and the
    new one. Giving the same file twice is a useful sanity check of the noise floor.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ExeA,
    [Parameter(Mandatory)][string]$ExeB,
    [string]$LabelA = 'A',
    [string]$LabelB = 'B',
    [int]$Runs = 3,
    [int]$HoverCycles = 12,
    [switch]$StopRunningInstance
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"
foreach ($exe in $ExeA, $ExeB) { if (-not (Test-Path -LiteralPath $exe)) { throw "Not found: $exe" } }
Assert-NoRealInstance -StopIt:$StopRunningInstance
$primary = Get-PrimaryRect

function Measure-Once($label, $exe) {
    $scratch = New-ScratchDir
    $appdata = Join-Path $scratch 'appdata'
    New-Item -ItemType Directory -Path "$appdata\EdgeDeck" -Force | Out-Null
    Set-Content -Path "$appdata\EdgeDeck\config.txt" -Encoding ASCII -Value @"
[settings]
theme=Dark
[tab]
widget=QuickActions
verticalRatio=0.30
tabWidth=26
tabHeight=76
panelWidth=300
[tab]
widget=QuickActions
verticalRatio=0.60
tabWidth=26
tabHeight=76
panelWidth=300
"@
    $real = $env:LOCALAPPDATA; $env:LOCALAPPDATA = $appdata
    try { $proc = Start-Process -FilePath $exe -PassThru } finally { $env:LOCALAPPDATA = $real }
    try {
        Start-Sleep -Seconds 6                                  # settle after startup
        $p = Get-Process -Id $proc.Id
        $startCpu = $p.TotalProcessorTime.TotalMilliseconds
        $wsIdle = $p.WorkingSet64 / 1MB; $privateIdle = $p.PrivateMemorySize64 / 1MB
        Start-Sleep -Seconds 10                                 # idle window
        $p.Refresh(); $idleCpu = $p.TotalProcessorTime.TotalMilliseconds - $startCpu

        $tab = @(Get-Wins 'EdgeDeckTabWindow' -ProcessId $proc.Id | Where-Object Visible | Sort-Object T)[0]
        $before = $p.TotalProcessorTime.TotalMilliseconds
        for ($i = 0; $i -lt $HoverCycles; $i++) {
            Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2); Start-Sleep -Milliseconds 450
            $panel = @(Get-Wins 'EdgeDeckPanelWindow' -ProcessId $proc.Id | Where-Object Visible)[0]
            if ($panel) {
                foreach ($row in 0, 1, 2) { Move-Mouse ($panel.L + 120) ($panel.T + 44 + $row * 44 + 22); Start-Sleep -Milliseconds 140 }
            }
            Move-Mouse ($primary.R / 2) ($primary.B / 2); Start-Sleep -Milliseconds 700
        }
        $p.Refresh()
        [pscustomobject]@{
            Build = $label
            IdleCpuMs = [math]::Round($idleCpu, 1)
            HoverCpuMs = [math]::Round($p.TotalProcessorTime.TotalMilliseconds - $before, 0)
            WorkingSetIdleMB = [math]::Round($wsIdle, 1)
            PrivateIdleMB = [math]::Round($privateIdle, 1)
            WorkingSetEndMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
            Threads = $p.Threads.Count
            Handles = $p.HandleCount
        }
    } finally {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 700
        Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$results = @()
for ($i = 0; $i -lt $Runs; $i++) {
    $results += Measure-Once $LabelA $ExeA
    $results += Measure-Once $LabelB $ExeB
}
$results | Format-Table -AutoSize
'--- means ---'
$results | Group-Object Build | ForEach-Object {
    $g = $_.Group
    '{0}: idleCPU={1:N1} ms/10s  hoverCPU={2:N0} ms/{3} cycles  WS idle={4:N1} MB  private idle={5:N1} MB  WS end={6:N1} MB  threads={7:N0}  handles={8:N0}' -f `
        $_.Name, ($g | Measure-Object IdleCpuMs -Average).Average, ($g | Measure-Object HoverCpuMs -Average).Average, $HoverCycles,
        ($g | Measure-Object WorkingSetIdleMB -Average).Average, ($g | Measure-Object PrivateIdleMB -Average).Average,
        ($g | Measure-Object WorkingSetEndMB -Average).Average, ($g | Measure-Object Threads -Average).Average,
        ($g | Measure-Object Handles -Average).Average
}

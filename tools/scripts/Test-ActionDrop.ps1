<#
.SYNOPSIS
    Checks that files dragged from Explorer onto the Quick Actions editor become rows.

.DESCRIPTION
    Runs settingsharness.exe hosting only the editor and has it perform a real OLE drag - the same
    data object Explorer hands over, the same lookup of the window under the pointer - onto several
    places in the window, then reads back the list, the status line and what Save keeps.

    1. A drop lands wherever it is let go: the list, each of the three edit boxes (which bring a
       drop target of their own, and are the reason every control is claimed), a label, a button
       and the empty window.
    2. A file takes its name without the extension, a folder keeps its whole name, and the row
       opens the dropped path.
    3. More files than the list has room for are not all taken, and the status line says how many
       were left out.
    4. A name longer than a row holds is cut to what a row holds.

    Moves the real cursor (the harness puts it where it drops). Safe beside the real EdgeDeck.
#>
[CmdletBinding()]
param([string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

$failures = 0
function Check($label, $ok, $detail = '') {
    '{0} {1}{2}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label, $(if (-not $ok -and $detail) { "  [$detail]" } else { '' })
    if (-not $ok) { $script:failures++ }
}

$harness = Get-Tool settingsharness $ToolsDir
$work = Join-Path ([IO.Path]::GetTempPath()) "edgedeck-drop-$PID"
New-Item -ItemType Directory -Force -Path $work | Out-Null

$file = Join-Path $work 'Mi Notas.txt'
Set-Content -LiteralPath $file -Value 'x'
$folder = Join-Path $work 'Carpeta v1.2'
New-Item -ItemType Directory -Force -Path $folder | Out-Null

function Invoke-Drop([int]$X, [int]$Y, [string[]]$Paths, [switch]$OleFirst) {
    $arg = 'dropfiles={0},{1},{2}' -f $X, $Y, ($Paths -join '|')
    $harnessArgs = @('dark', 2, 'window=actions', $arg)
    if ($OleFirst) { $harnessArgs += 'ole' }   # OLE running before the controls are made
    $lines = & $harness @harnessArgs 2>&1 | ForEach-Object { "$_" }
    $items = @($lines | Where-Object { $_ -match '^\s+item: ' })
    $actions = @($lines | Where-Object { $_ -match '^\s+action: ' })
    $status = ($lines | Where-Object { $_ -like 'status: *' } | Select-Object -First 1)
    $effect = if (($lines | Where-Object { $_ -match 'effect=(\d+)' }) -match 'effect=(\d+)') { [int]$Matches[1] } else { -1 }
    return @{ Lines = $lines; Items = $items; Actions = $actions; Status = $status; Effect = $effect }
}

try {
    # The editor starts with four rows (the three defaults and one with an accent). One file added.
    $places = @(
        @{ Name = 'the list';             X = 100; Y = 100 },
        @{ Name = 'the Name box';         X = 350; Y = 41 },
        @{ Name = 'the Opens box';        X = 300; Y = 91 },
        @{ Name = 'the Arguments box';    X = 350; Y = 141 },
        @{ Name = 'the explanation text'; X = 350; Y = 230 },
        @{ Name = 'the Add button';       X = 40;  Y = 286 },
        @{ Name = 'the empty bottom edge'; X = 20; Y = 346 }
    )
    foreach ($place in $places) {
        $r = Invoke-Drop $place.X $place.Y @($file)
        $took = ($r.Items.Count -eq 5) -and ($r.Actions.Count -eq 5) -and ($r.Actions[-1] -like "*Mi Notas | $file |*")
        Check "a file dropped on $($place.Name) becomes a row" $took ("items={0} last={1}" -f $r.Items.Count, ($r.Actions | Select-Object -Last 1))
    }

    # The same on the edit boxes when OLE was already running as they were made: a control that
    # registered a drop target of its own at that point would shadow the window's.
    foreach ($place in @($places[1], $places[2], $places[3])) {
        $r = Invoke-Drop $place.X $place.Y @($file) -OleFirst
        Check "a file dropped on $($place.Name), with OLE started first, becomes a row" ($r.Items.Count -eq 5) "items=$($r.Items.Count)"
    }

    # What the drag told the source: a reference was made, nothing was copied or moved.
    $r = Invoke-Drop 100 100 @($file)
    Check 'the drag ends as a link (nothing is copied or moved)' ($r.Effect -eq 4) "effect=$($r.Effect)"
    Check 'the status line names what was added and says to save' ($r.Status -like 'status: Added "Mi Notas"*Save*') "$($r.Status)"

    # Name rules: a folder keeps its dot.
    $r = Invoke-Drop 100 100 @($folder)
    Check 'a folder keeps its whole name, dot included' ($r.Actions.Count -eq 5 -and $r.Actions[-1] -like '*Carpeta v1.2 | *') ($r.Actions | Select-Object -Last 1)

    # Several at once, in the order they were dragged.
    $r = Invoke-Drop 100 100 @($file, $folder)
    Check 'two dropped together are two rows, in order' ($r.Actions.Count -eq 6 -and $r.Actions[-2] -like '*Mi Notas | *' -and $r.Actions[-1] -like '*Carpeta v1.2 | *')
    Check 'and the status says how many' ($r.Status -like 'status: Added 2 actions*') "$($r.Status)"

    # A name longer than a row holds.
    $long = Join-Path $work (('n' * 70) + '.txt')
    Set-Content -LiteralPath $long -Value 'x'
    $r = Invoke-Drop 100 100 @($long)
    $name = if ($r.Actions.Count -gt 0) { (($r.Actions[-1] -replace '^\s+action: ', '') -split ' \| ')[0] } else { '' }
    Check 'a name longer than a row holds is cut to 40 characters' ($name.Length -eq 40) "length $($name.Length)"

    # Too many: four rows, room for eight; nine dragged.
    $many = 1..9 | ForEach-Object {
        $p = Join-Path $work ("f{0}.txt" -f $_)
        Set-Content -LiteralPath $p -Value 'x'
        $p
    }
    $r = Invoke-Drop 100 100 $many
    Check 'the list is filled to its limit of twelve and no further' ($r.Actions.Count -eq 12) "$($r.Actions.Count) rows"
    Check 'and the status says one was left out' ($r.Status -like 'status: Added 8*1 was left out*') "$($r.Status)"
} finally {
    Get-ChildItem -LiteralPath $work -Force -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.PSIsContainer) { [IO.Directory]::Delete($_.FullName, $true) } else { [IO.File]::Delete($_.FullName) }
    }
    [IO.Directory]::Delete($work, $true)
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

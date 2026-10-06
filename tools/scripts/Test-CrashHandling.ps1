<#
.SYNOPSIS
    Crashes a test copy of EdgeDeck on purpose and checks what it leaves behind: a log line, a
    minidump, and one - only one - restart.

.DESCRIPTION
    Uses apptest.exe, which installs the crash handler exactly as EdgeDeck.exe does and can be
    told to crash by dropping a file in its trigger folder. Everything runs in a scratch folder
    (LOCALAPPDATA is redirected), so it never touches the real log or the real dumps.

      1. a real access violation  -> a "Crash:" line naming it, a dump, a new copy started,
                                     and that copy told it was started by a crash
      2. the new copy crashes again at once -> NOT restarted a second time (no restart loop)
      3. restartAfterCrash=0 in the config + an uncaught exception (std::terminate)
                                  -> logged as that, a dump, and no new copy

    Kills only processes named apptest.exe, and only ones it started. Safe beside the real
    EdgeDeck; opens a window for a moment.
#>
[CmdletBinding()]
param([string]$ToolsDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

$failures = 0
function Check($label, $ok) {
    '{0} {1}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label
    if (-not $ok) { $script:failures++ }
}
function Get-ApptestProcesses($app) {
    # Match on the trigger folder in the command line: it is unique to this run, so a copy that
    # started by itself (the restart) is found, and nothing else is.
    @(Get-CimInstance Win32_Process -Filter "Name='apptest.exe'" |
        Where-Object { $_.CommandLine -like "*$($app.Trigger)*" })
}
function Wait-NewCopy($app, $notPid, $seconds) {
    $found = $null
    [void](Wait-Until -TimeoutSeconds $seconds -Condition {
        $script:found = Get-ApptestProcesses $app | Where-Object { $_.ProcessId -ne $notPid } | Select-Object -First 1
        [bool]$script:found
    })
    return $script:found
}
function Log-Text($app) { if (Test-Path $app.Log) { Get-Content $app.Log -Raw } else { '' } }
function Dumps($app) { @(Get-ChildItem (Join-Path $app.AppData 'EdgeDeck\crashes') -Filter *.dmp -ErrorAction SilentlyContinue) }

# ---- 1 & 2: a crash restarts once, and a crash right after that does not restart again ----
$app = Start-TestApp -Tabs QuickActions
try {
    $first = $app.Process
    Set-Content -Path (Join-Path $app.Trigger 'crash.txt') -Value 'x'
    Check 'the crashed copy exited' ($first.WaitForExit(10000))
    Check 'it exited with the access violation code' ($first.HasExited -and $first.ExitCode -eq -1073741819)

    $log = Log-Text $app
    Check 'the log names the exception and where'   ($log -match 'Crash: access violation \(0xC0000005\) at apptest\.exe\+0x[0-9A-F]+, thread \d+, up \d+ s, build 0x[0-9A-F]{8}')
    Check 'a dump was written and logged'          ($log -match 'Crash: dump written to .+\.dmp')
    $dump = Dumps $app | Select-Object -First 1
    Check 'the dump file exists and is not empty'  ($dump -and $dump.Length -gt 10KB)
    Check 'the log says a new copy was started'    ($log -match 'Crash: started a new copy')

    $second = Wait-NewCopy $app $first.Id 10
    Check 'a new copy is running'                  ([bool]$second)
    Check 'it was started with the restart flags'  ($second -and $second.CommandLine -match '--restarted-after-crash --previous-pid=\d+')
    Start-Sleep -Seconds 3
    Check 'it knows it was started by a crash'     ((Log-Text $app) -match 'Started after the previous copy crashed')

    # The restarted copy dies straight away (as it would if it failed at startup).
    Set-Content -Path (Join-Path $app.Trigger 'crash.txt') -Value 'x'
    Start-Sleep -Seconds 5
    $log = Log-Text $app
    Check 'the second crash was logged too'        (([regex]::Matches($log, 'Crash: access violation')).Count -eq 2)
    Check 'and was NOT restarted a second time'    ($log -match 'Crash: not restarting' -and (Get-ApptestProcesses $app).Count -eq 0)
    Check 'two dumps now exist'                    ((Dumps $app).Count -eq 2)
} finally {
    Get-ApptestProcesses $app | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Remove-Item $app.Scratch -Recurse -Force -ErrorAction SilentlyContinue
}

# ---- 3: restarting switched off, and an uncaught exception instead of an access violation ----
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'restartAfterCrash=0')
try {
    Set-Content -Path (Join-Path $app.Trigger 'terminate.txt') -Value 'x'
    Check 'the copy exited' ($app.Process.WaitForExit(10000))
    Check 'with the std::terminate exit code, not a bare fail-fast (0xC0000409)' ($app.Process.HasExited -and ($app.Process.ExitCode -band 0xFFFFFFFF) -eq 0xE0000001)
    Start-Sleep -Seconds 3
    $log = Log-Text $app
    Check 'an uncaught exception is logged as std::terminate' ($log -match 'Crash: unhandled C\+\+ exception \(std::terminate\) \(0xE0000001\)')
    Check 'it still gets a dump'                              ((Dumps $app).Count -eq 1)
    Check 'restartAfterCrash=0 is honoured: no new copy'      ($log -match 'Crash: not restarting' -and (Get-ApptestProcesses $app).Count -eq 0)
} finally {
    Get-ApptestProcesses $app | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Remove-Item $app.Scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

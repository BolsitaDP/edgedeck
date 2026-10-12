<#
.SYNOPSIS
    Removes EdgeDeck: stops it, takes it off the startup list and the desktop, and deletes the
    installed copy.

.DESCRIPTION
    Undoes what install.ps1 did:

      1. stops a running EdgeDeck;
      2. removes the "Start with Windows" entry, and the mark Task Manager keeps beside it;
      3. removes the desktop shortcut, if it points at the installed copy (one the user pointed
         somewhere else is theirs);
      4. deletes the installed program, %LOCALAPPDATA%\EdgeDeck\bin.

    Settings (config.txt), the log and the lyrics cache stay, so installing again picks up where
    it left off; -RemoveSettings deletes those too.

.PARAMETER RemoveSettings
    Also delete %LOCALAPPDATA%\EdgeDeck entirely: settings, log, crash dumps, lyrics cache.

.PARAMETER AllowSandbox
    Go ahead although this PowerShell runs inside a packaged app, whose redirected view of
    %LOCALAPPDATA% and HKCU is not what Windows sees (see install.ps1).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\uninstall.ps1
#>
[CmdletBinding()]
param(
    [switch]$RemoveSettings,
    [switch]$AllowSandbox
)

$ErrorActionPreference = 'Stop'

$dataDir = Join-Path $env:LOCALAPPDATA 'EdgeDeck'
$installDir = Join-Path $dataDir 'bin'
$installedExe = Join-Path $installDir 'EdgeDeck.exe'

# The same trap as install.ps1: from a packaged app's terminal this would "remove" the app's
# private copy of things and leave the real ones alone.
if (-not $AllowSandbox) {
    $packages = Join-Path (Split-Path -Parent $env:LOCALAPPDATA) 'Local\Packages'
    $name = "edgedeck-redirect-probe-$([guid]::NewGuid().ToString('N')).tmp"
    $marker = Join-Path $env:LOCALAPPDATA $name
    $redirected = $false
    try {
        if (Test-Path -LiteralPath $packages) {
            Set-Content -LiteralPath $marker -Value 'x'
            foreach ($package in Get-ChildItem -LiteralPath $packages -Directory -ErrorAction SilentlyContinue) {
                if (Test-Path -LiteralPath (Join-Path $package.FullName "LocalCache\Local\$name")) { $redirected = $true; break }
            }
        }
    } catch {
        $redirected = $false
    } finally {
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
    }
    if ($redirected) {
        throw ('This PowerShell is running inside a packaged app, which redirects %LOCALAPPDATA% and HKCU ' +
               'into its own private store, so uninstalling from here would not touch the real installation. ' +
               'Run this script from a normal PowerShell or Windows Terminal window (or pass -AllowSandbox).')
    }
}

# --- 1. stop --------------------------------------------------------------------------------
foreach ($process in @(Get-Process -Name EdgeDeck -ErrorAction SilentlyContinue)) {
    Write-Host "Stopping EdgeDeck (pid $($process.Id))"
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
}

# --- 2. startup entry and Task Manager's mark -----------------------------------------------
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$approvedKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
foreach ($key in $runKey, $approvedKey) {
    if (Get-ItemProperty -Path $key -Name EdgeDeck -ErrorAction SilentlyContinue) {
        Remove-ItemProperty -Path $key -Name EdgeDeck -ErrorAction SilentlyContinue
    }
}
Write-Host 'Startup entry: removed (if there was one)'

# --- 3. shortcut ----------------------------------------------------------------------------
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'EdgeDeck.lnk'
if (Test-Path -LiteralPath $shortcutPath) {
    $target = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath).TargetPath
    if ($target -and ([IO.Path]::GetFullPath($target) -ieq [IO.Path]::GetFullPath($installedExe))) {
        Remove-Item -LiteralPath $shortcutPath -Force
        Write-Host "Shortcut: removed $shortcutPath"
    } else {
        Write-Host "Shortcut: left alone, it does not point at the installed copy ($target)"
    }
}

# --- 4. files -------------------------------------------------------------------------------
if (Test-Path -LiteralPath $installDir) {
    Remove-Item -LiteralPath $installDir -Recurse -Force
    Write-Host "Removed: $installDir"
}
if ($RemoveSettings -and (Test-Path -LiteralPath $dataDir)) {
    Remove-Item -LiteralPath $dataDir -Recurse -Force
    Write-Host "Removed: $dataDir (settings, log, crash dumps, lyrics cache)"
} elseif (Test-Path -LiteralPath $dataDir) {
    Write-Host "Kept: $dataDir (settings and log). -RemoveSettings deletes them too."
}

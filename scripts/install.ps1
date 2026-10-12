<#
.SYNOPSIS
    Installs EdgeDeck into a fixed folder, so rebuilding never touches the copy that is running.

.DESCRIPTION
    Windows will not let a build overwrite an executable that is running. Running EdgeDeck
    straight out of build\Release therefore meant every rebuild had to kill it first. This
    copies the build to %LOCALAPPDATA%\EdgeDeck\bin and runs it from there instead:

      1. -Build   builds first (CMake + MSVC). Safe while EdgeDeck is running, because the
                  running copy is the installed one and not the build output.
                  (Not needed in a release package, which carries EdgeDeck.exe next to this script.)
      2. Stops any running EdgeDeck. Only one can run at a time (it holds a single-instance
         lock), and an old copy left running would make the new one exit silently.
      3. Copies build\<Configuration>\EdgeDeck.exe into the install folder.
      4. Repoints the desktop shortcut, and the "Start with Windows" entry if it is enabled.
      5. Starts EdgeDeck again.

    Settings are not touched: they live in %LOCALAPPDATA%\EdgeDeck\config.txt, next to the
    bin folder.

.PARAMETER Build
    Build before installing.

.PARAMETER NoLaunch
    Install but do not start EdgeDeck afterwards.

.PARAMETER CreateShortcut
    Create the desktop shortcut if there is none. An existing one is always repointed; a
    missing one is left missing unless this is given, in case it was deleted on purpose.

.PARAMETER EnableAutostart
    Turn "Start with Windows" on: create the startup entry (or repoint it) and clear the mark
    Task Manager leaves when someone disables it, so the entry means what it says. Without
    this switch an entry is only ever repointed, never created, so that installing does not
    undo a choice made in Settings.

.PARAMETER ExePath
    The EdgeDeck.exe to install. By default: the EdgeDeck.exe next to this script (a release
    package) if there is one, otherwise the build output.

.PARAMETER InstallDir
    Where to put it. Default: %LOCALAPPDATA%\EdgeDeck\bin, which is also where the app looks for
    its installed copy, so it is not something to change outside of testing the installer.

.PARAMETER NoIntegration
    Copy the files and nothing else: do not touch the desktop shortcut or the startup entry, and
    do not stop a running EdgeDeck. For trying the installer out without changing the machine.

.PARAMETER AllowSandbox
    Go ahead although this PowerShell runs inside a packaged app (see the check below). What it
    writes under %LOCALAPPDATA% and HKCU then goes to that app's private copy, which Windows
    does not read at sign-in; only useful for trying the script out.

.PARAMETER Configuration
    Build configuration to install. Default: Release.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Build
#>
[CmdletBinding()]
param(
    [switch]$Build,
    [switch]$NoLaunch,
    [switch]$CreateShortcut,
    [switch]$EnableAutostart,
    [switch]$AllowSandbox,
    [switch]$NoIntegration,
    [string]$ExePath,
    [string]$InstallDir,
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo 'build'
# Which executable: the one asked for; else the one next to this script (a release package puts
# them side by side); else what the build produced. The sibling is checked first so that unpacking
# a release inside some other project's folder tree still installs the release.
$packagedExe = Join-Path $PSScriptRoot 'EdgeDeck.exe'
if ($ExePath) {
    $builtExe = $ExePath
} elseif (Test-Path -LiteralPath $packagedExe) {
    $builtExe = $packagedExe
} else {
    $builtExe = Join-Path $buildDir "$Configuration\EdgeDeck.exe"
}
$installDir = if ($InstallDir) { $InstallDir } else { Join-Path $env:LOCALAPPDATA 'EdgeDeck\bin' }
$installedExe = Join-Path $installDir 'EdgeDeck.exe'

# --- 0. is what this writes going to be seen by Windows? -----------------------------------
# A PowerShell started from inside a packaged (MSIX) app - the Claude desktop app's terminal, for
# one - has its writes under %LOCALAPPDATA% and in HKCU redirected into that app's private store.
# Nothing fails: the copy "succeeds", the Run entry "is created", and at the next sign-in Windows
# finds no executable and no entry, so EdgeDeck simply does not start. That is exactly how this
# once went unnoticed for days. Detected by writing a marker file and looking for it where the
# redirect keeps its files.
function Test-RedirectedAppData {
    $packages = Join-Path (Split-Path -Parent $env:LOCALAPPDATA) 'Local\Packages'
    if (-not (Test-Path -LiteralPath $packages)) { return $false }
    $name = "edgedeck-redirect-probe-$([guid]::NewGuid().ToString('N')).tmp"
    $marker = Join-Path $env:LOCALAPPDATA $name
    try {
        Set-Content -LiteralPath $marker -Value 'x' -ErrorAction Stop
        foreach ($package in Get-ChildItem -LiteralPath $packages -Directory -ErrorAction SilentlyContinue) {
            if (Test-Path -LiteralPath (Join-Path $package.FullName "LocalCache\Local\$name")) { return $true }
        }
        return $false
    } catch {
        return $false
    } finally {
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
    }
}
if ((Test-RedirectedAppData) -and -not $AllowSandbox -and -not $NoIntegration) {
    throw ("This PowerShell is running inside a packaged app, which redirects %LOCALAPPDATA% and HKCU " +
           "into its own private store: installing from here would look like it worked and Windows " +
           "would not see the program or its startup entry. Run this script from a normal PowerShell " +
           "or Windows Terminal window instead (or pass -AllowSandbox to try it anyway).")
}

# --- 1. build -----------------------------------------------------------------------------
if ($Build) {
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }
    if (-not (Test-Path -LiteralPath $cmake)) { throw 'CMake was not found. Install it or add it to PATH.' }

    # A copy still running out of the build folder (how it used to be run, and how it
    # is on the first install) holds the file the linker has to replace. That is the
    # one case where the build must stop it first; the installed copy never blocks.
    $fromBuildFolder = @(Get-Process -Name EdgeDeck -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $builtExe })
    foreach ($process in $fromBuildFolder) {
        Write-Host "Stopping EdgeDeck (pid $($process.Id)), which is running from the build folder"
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
    }

    Write-Host 'Building...'
    & $cmake -S $repo -B $buildDir -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmake --build $buildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; nothing was installed and the running copy was left alone.' }
}

if (-not (Test-Path -LiteralPath $builtExe)) {
    throw "No EdgeDeck.exe found at $builtExe. Build it with -Build, or run this from a release package."
}

# --- 2. stop whatever is running ----------------------------------------------------------
# Not with -NoIntegration: trying the installer out is not a reason to stop the user's own copy.
$running = @()
if (-not $NoIntegration) { $running = @(Get-Process -Name EdgeDeck -ErrorAction SilentlyContinue) }
foreach ($process in $running) {
    Write-Host "Stopping EdgeDeck (pid $($process.Id))"
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
}
if ($running.Count -gt 0) { $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue }

# --- 3. copy ------------------------------------------------------------------------------
New-Item -ItemType Directory -Force -Path $installDir | Out-Null

# The file can stay locked for a moment after its process ends (antivirus, the shell), so a
# few quick retries are worth more than failing on the first.
$copied = $false
for ($attempt = 0; $attempt -lt 20 -and -not $copied; $attempt++) {
    try {
        Copy-Item -LiteralPath $builtExe -Destination $installedExe -Force
        $copied = $true
    } catch {
        Start-Sleep -Milliseconds 250
    }
}
if (-not $copied) { throw "Could not write $installedExe. Is something still holding it open?" }
Write-Host "Installed: $installedExe"

# The symbols that go with exactly this executable. A crash line and its minidump say where in
# EdgeDeck.exe it stopped; only the .pdb of the SAME build can say which function that is, and
# the next build would overwrite the one in build\Release.
$builtPdb = [IO.Path]::ChangeExtension($builtExe, '.pdb')
if (Test-Path -LiteralPath $builtPdb) {
    Copy-Item -LiteralPath $builtPdb -Destination (Join-Path $installDir 'EdgeDeck.pdb') -Force -ErrorAction SilentlyContinue
}

# --- 4. shortcut and autostart ------------------------------------------------------------
if ($NoIntegration) {
    Write-Host 'Left the shortcut, the startup entry and any running copy alone (-NoIntegration).'
    return
}
$shell = New-Object -ComObject WScript.Shell
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'EdgeDeck.lnk'
if ((Test-Path -LiteralPath $shortcutPath) -or $CreateShortcut) {
    # Loading an existing shortcut keeps whatever else was set on it (window style, hotkey).
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $installedExe
    $shortcut.WorkingDirectory = $installDir
    $shortcut.IconLocation = "$installedExe,0"
    if (-not $shortcut.Description) { $shortcut.Description = 'EdgeDeck' }
    $shortcut.Save()
    Write-Host "Shortcut:  $shortcutPath"
}

$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$hadEntry = [bool](Get-ItemProperty -Path $runKey -Name EdgeDeck -ErrorAction SilentlyContinue)
if ($hadEntry -or $EnableAutostart) {
    # Quoted, as the app itself writes it: the path may contain spaces.
    Set-ItemProperty -Path $runKey -Name EdgeDeck -Value "`"$installedExe`""
    Write-Host $(if ($hadEntry) { 'Autostart: repointed' } else { 'Autostart: enabled' })
}
if ($EnableAutostart) {
    # Task Manager's Startup tab does not delete the entry to disable it: it writes a mark beside
    # it, an odd first byte, and leaves the entry where it was. An entry carrying that mark is
    # ignored at sign-in, so enabling means removing the mark too (as the app's own Settings does).
    $approved = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run'
    if (Get-ItemProperty -Path $approved -Name EdgeDeck -ErrorAction SilentlyContinue) {
        Remove-ItemProperty -Path $approved -Name EdgeDeck -ErrorAction SilentlyContinue
        Write-Host 'Autostart: cleared the "disabled in Task Manager" mark'
    }
}

# --- 5. start -----------------------------------------------------------------------------
if (-not $NoLaunch) {
    Start-Process -FilePath $installedExe -WorkingDirectory $installDir
    Write-Host 'Started.'
}

<#
.SYNOPSIS
    Installs EdgeDeck into a fixed folder, so rebuilding never touches the copy that is running.

.DESCRIPTION
    Windows will not let a build overwrite an executable that is running. Running EdgeDeck
    straight out of build\Release therefore meant every rebuild had to kill it first. This
    copies the build to %LOCALAPPDATA%\EdgeDeck\bin and runs it from there instead:

      1. -Build   builds first (CMake + MSVC). Safe while EdgeDeck is running, because the
                  running copy is the installed one and not the build output.
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
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo 'build'
$builtExe = Join-Path $buildDir "$Configuration\EdgeDeck.exe"
$installDir = Join-Path $env:LOCALAPPDATA 'EdgeDeck\bin'
$installedExe = Join-Path $installDir 'EdgeDeck.exe'

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
    throw "No build found at $builtExe. Run again with -Build."
}

# --- 2. stop whatever is running ----------------------------------------------------------
$running = @(Get-Process -Name EdgeDeck -ErrorAction SilentlyContinue)
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

# --- 4. shortcut and autostart ------------------------------------------------------------
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
if (Get-ItemProperty -Path $runKey -Name EdgeDeck -ErrorAction SilentlyContinue) {
    # Quoted, as the app itself writes it: the path may contain spaces.
    Set-ItemProperty -Path $runKey -Name EdgeDeck -Value "`"$installedExe`""
    Write-Host 'Autostart: repointed'
}

# --- 5. start -----------------------------------------------------------------------------
if (-not $NoLaunch) {
    Start-Process -FilePath $installedExe -WorkingDirectory $installDir
    Write-Host 'Started.'
}

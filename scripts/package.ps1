<#
.SYNOPSIS
    Builds the release package: a zip with EdgeDeck.exe and what is needed to install it on a
    machine that has no compiler.

.DESCRIPTION
    Writes, into dist\ (git-ignored):

      EdgeDeck-<version>-win-<arch>.zip   EdgeDeck.exe, Install.cmd, install.ps1, uninstall.ps1,
                                          README.txt, LICENSE.txt. Unzip anywhere and run Install.cmd.
      EdgeDeck-<version>-win-<arch>.pdb   the symbols for exactly that executable, kept apart
                                          because they are 12 MB and only a crash dump needs them.

    The executable needs nothing else: the C/C++ runtime is linked in, so there is no
    redistributable to install first.

    The version is -Version, else the one the build was configured with, else 1.0.0.

.PARAMETER Version
    major.minor.patch. With -Build it is also what the build is stamped with.

.PARAMETER Architecture
    x64 (the default) or arm64, for Windows on ARM. Each has its own build folder (build,
    build-arm64) and its own package name (win-x64, win-arm64).

.PARAMETER Build
    Configure and build first (Release). Without it the existing build\Release is packaged, so
    the package matches whatever was last built - say so with -Version if it matters.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Build -Version 1.2.0
#>
[CmdletBinding()]
param(
    [string]$Version,
    [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64',
    [switch]$Build,
    [string]$Configuration = 'Release',
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo $(if ($Architecture -eq 'x64') { 'build' } else { 'build-arm64' })
$cmakeArch = if ($Architecture -eq 'x64') { 'x64' } else { 'ARM64' }
if (-not $OutDir) { $OutDir = Join-Path $repo 'dist' }

# The version of an existing build, so a package made from it says what it is.
function Get-ConfiguredVersion {
    $cache = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $line = Select-String -Path $cache -Pattern '^EDGEDECK_VERSION:[A-Z]+=(.+)$' | Select-Object -First 1
        if ($line) { return $line.Matches[0].Groups[1].Value.Trim() }
    }
    return '1.0.0'
}
if (-not $Version) { $Version = Get-ConfiguredVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must be major.minor.patch, got '$Version'." }

if ($Build) {
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }
    if (-not (Test-Path -LiteralPath $cmake)) { throw 'CMake was not found. Install it or add it to PATH.' }
    # Quoted: PowerShell otherwise splits -DNAME=1.2.0 at the dots before CMake sees it.
    & $cmake -S $repo -B $buildDir -G 'Visual Studio 17 2022' -A $cmakeArch "-DEDGEDECK_VERSION=$Version"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmake --build $buildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; nothing was packaged.' }
}

$exe = Join-Path $buildDir "$Configuration\EdgeDeck.exe"
$pdb = [IO.Path]::ChangeExtension($exe, '.pdb')
if (-not (Test-Path -LiteralPath $exe)) { throw "No build found at $exe. Run again with -Build." }

# What the file itself says its version is: a package named 1.2.0 around a 1.0.0 executable is the
# mistake this exists to catch.
$fileVersion = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($fileVersion -ne $Version) {
    throw "The executable at $exe is version $fileVersion, not $Version. Build it with -Build -Version $Version."
}

$name = "EdgeDeck-$Version-win-$Architecture"
$stage = Join-Path $OutDir $name
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'EdgeDeck.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'install.ps1') -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'uninstall.ps1') -Destination $stage
# The licence travels with every copy: MIT asks for the notice to be included.
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'LICENSE') -Destination (Join-Path $stage 'LICENSE.txt')

# A double-click for people who do not want to open a terminal: asks the one question that matters
# and runs the installer with the right switches. cmd, not PowerShell, because a .ps1 is opened in
# an editor by default and the execution policy would stop it anyway.
@'
@echo off
setlocal
rem Double-clicked inside the zip, Explorer extracts this file alone, to a temporary folder.
if not exist "%~dp0EdgeDeck.exe" (
    echo EdgeDeck.exe is not next to this file: it looks like it was opened from inside the zip.
    echo Unzip the whole zip first ^(right-click it, Extract All^), then run Install.cmd from there.
    echo.
    pause
    exit /b 1
)
echo EdgeDeck installs for your user account only (no administrator needed).
echo.
choice /C YN /M "Start EdgeDeck with Windows"
if errorlevel 2 (set "AUTOSTART=") else (set "AUTOSTART=-EnableAutostart")
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -CreateShortcut %AUTOSTART%
echo.
pause
'@ | Set-Content -LiteralPath (Join-Path $stage 'Install.cmd') -Encoding ASCII

@"
EdgeDeck $Version

Edge tabs for Windows: small tabs on the screen edge, each opening a panel when the pointer rests
on it (quick actions, media, volume, brightness, lyrics, monitors).

Install:    double-click Install.cmd
Uninstall:  Settings > Apps > Installed apps > EdgeDeck > Uninstall (or run uninstall.ps1).

Installs to %LOCALAPPDATA%\EdgeDeck\bin for the current user, creates a desktop shortcut, and
(if you say yes) starts with Windows. Nothing outside your user profile is touched and no
administrator rights are needed. Settings live in %LOCALAPPDATA%\EdgeDeck\config.txt.

If Windows SmartScreen warns about an unrecognised app: the executable is not code-signed. Check
the SHA256 against the one published with the release.
"@ | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding ASCII

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -LiteralPath $stage -Recurse -Force

if (Test-Path -LiteralPath $pdb) {
    Copy-Item -LiteralPath $pdb -Destination (Join-Path $OutDir "$name.pdb") -Force
}

$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content -LiteralPath (Join-Path $OutDir "$name.zip.sha256") -Encoding ASCII

Write-Host "Package: $zip ($([math]::Round((Get-Item -LiteralPath $zip).Length / 1KB)) KB)"
Write-Host "SHA256:  $hash"

<#
.SYNOPSIS
    Builds EdgeDeck's setup program, EdgeDeck-<version>-setup-<arch>.exe, with Inno Setup.

.DESCRIPTION
    Compiles installer\EdgeDeck.iss around an already built EdgeDeck.exe and writes, into dist\
    (git-ignored), the setup program and its .sha256. The executable's own version must be the one
    asked for, as for the zip (package.ps1).

    Needs Inno Setup 6 (https://jrsoftware.org/isinfo.php): ISCC.exe is looked for at -Iscc, on
    PATH, and where Inno Setup's installer puts it, for all users or for one.

.PARAMETER Version
    major.minor.patch. Default: the version the build was configured with.

.PARAMETER Architecture
    x64 (the default) or arm64; the executable is taken from build\ or build-arm64\.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\installer.ps1 -Version 1.0.0
#>
[CmdletBinding()]
param(
    [string]$Version,
    [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64',
    [string]$Configuration = 'Release',
    [string]$Iscc,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo $(if ($Architecture -eq 'x64') { 'build' } else { 'build-arm64' })
if (-not $OutDir) { $OutDir = Join-Path $repo 'dist' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$exe = Join-Path $buildDir "$Configuration\EdgeDeck.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "No build found at $exe. Build it first (or run package.ps1 -Build)." }
$fileVersion = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if (-not $Version) { $Version = $fileVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must be major.minor.patch, got '$Version'." }
if ($fileVersion -ne $Version) {
    throw "The executable at $exe is version $fileVersion, not $Version. Build it with that version first."
}

if (-not $Iscc) {
    $candidates = @(
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $Iscc = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
}
if (-not $Iscc) { throw 'Inno Setup 6 was not found (ISCC.exe). Install it from https://jrsoftware.org/isinfo.php, or pass -Iscc.' }

$homepage = 'https://github.com/BolsitaDP/edgedeck'
$cache = Join-Path $buildDir 'CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $line = Select-String -Path $cache -Pattern '^EDGEDECK_HOMEPAGE:[A-Z]+=(.+)$' | Select-Object -First 1
    if ($line) { $homepage = $line.Matches[0].Groups[1].Value.Trim() }
}

& $Iscc /Q "/DAppVersion=$Version" "/DArch=$Architecture" "/DSourceExe=$exe" "/DOutputDir=$OutDir" "/DHomepage=$homepage" `
    (Join-Path $repo 'installer\EdgeDeck.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)." }

$name = "EdgeDeck-$Version-setup-$Architecture.exe"
$setup = Join-Path $OutDir $name
$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name" | Set-Content -LiteralPath "$setup.sha256" -Encoding ASCII
Write-Host "Setup:  $setup ($([math]::Round((Get-Item -LiteralPath $setup).Length / 1KB)) KB)"
Write-Host "SHA256: $hash"

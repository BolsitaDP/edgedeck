<#
.SYNOPSIS
    Writes the winget manifests for a published release, ready to submit to microsoft/winget-pkgs.

.DESCRIPTION
    winget installs EdgeDeck with the release's own setup program: it downloads
    EdgeDeck-<version>-setup-<arch>.exe from GitHub, checks it against the SHA256 in the manifest
    and runs it silently - for the current user, without administrator rights. An upgrade is that
    setup again, which closes a running EdgeDeck cleanly and starts the new one; Windows' list of
    installed apps knows it by the setup's AppId, which is the manifest's ProductCode. Settings
    live where they always do, in %LOCALAPPDATA%\EdgeDeck.

    The checksums are read from the .sha256 files installer.ps1 wrote next to the setups, so run this
    on the same dist\ that was published - in CI that is the release job itself, which attaches
    the result to the release.

    Writes into <OutDir>\manifests\<letter>\<publisher>\<name>\<version>\, the layout winget-pkgs
    uses, and validates with `winget validate` when winget is installed.

.PARAMETER Version
    major.minor.patch, as in the tag without its "v".

.PARAMETER Homepage
    The project page on GitHub; the setups are expected under <Homepage>/releases/download/v<Version>/.

.PARAMETER PackageIdentifier
    Publisher.Name, as winget-pkgs knows the package. A fork submits under its own.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\winget.ps1 -Version 1.0.0
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Version,
    [string]$Homepage = 'https://github.com/BolsitaDP/edgedeck',
    [string]$PackageIdentifier = 'BolsitaDP.EdgeDeck',
    [string]$PackageDir,
    [string]$OutDir,
    [string]$ReleaseDate = (Get-Date -Format 'yyyy-MM-dd')
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $PackageDir) { $PackageDir = Join-Path $repo 'dist' }
if (-not $OutDir) { $OutDir = Join-Path $PackageDir 'winget' }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must be major.minor.patch, got '$Version'." }
if ($PackageIdentifier -notmatch '^[^.\s]+\.[^.\s]+$') { throw "PackageIdentifier must be Publisher.Name, got '$PackageIdentifier'." }
$Homepage = $Homepage.TrimEnd('/')
$publisherId, $nameId = $PackageIdentifier.Split('.')
$owner = ([Uri]$Homepage).AbsolutePath.Trim('/').Split('/')[0]

# One installer per architecture that was packaged; x64 must be there.
$installers = @()
foreach ($arch in 'x64', 'arm64') {
    $setup = "EdgeDeck-$Version-setup-$arch.exe"
    $sumFile = Join-Path $PackageDir "$setup.sha256"
    if (-not (Test-Path -LiteralPath $sumFile)) {
        if ($arch -eq 'x64') { throw "No $sumFile. Run installer.ps1 -Version $Version first." }
        continue
    }
    $hash = ((Get-Content -LiteralPath $sumFile -Raw).Trim() -split '\s+')[0].ToUpperInvariant()
    if ($hash -notmatch '^[0-9A-F]{64}$') { throw "$sumFile does not hold a SHA256." }
    $installers += @"
- Architecture: $arch
  InstallerUrl: $Homepage/releases/download/v$Version/$setup
  InstallerSha256: $hash
"@
}

$schema = '1.6.0'
$dir = Join-Path $OutDir ("manifests\{0}\{1}\{2}\{3}" -f $publisherId.Substring(0, 1).ToLowerInvariant(), $publisherId, $nameId, $Version)
if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dir | Out-Null

function Write-Manifest($fileName, $text) {
    # UTF-8 without a BOM and LF line ends, as winget-pkgs' validation expects.
    [IO.File]::WriteAllText((Join-Path $dir $fileName), ($text.Trim() -replace "`r`n", "`n") + "`n", (New-Object Text.UTF8Encoding $false))
}

Write-Manifest "$PackageIdentifier.yaml" @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.$schema.schema.json
PackageIdentifier: $PackageIdentifier
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: $schema
"@

Write-Manifest "$PackageIdentifier.installer.yaml" @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.$schema.schema.json
PackageIdentifier: $PackageIdentifier
PackageVersion: $Version
MinimumOSVersion: 10.0.17763.0
InstallerType: inno
Scope: user
InstallModes:
- interactive
- silent
- silentWithProgress
UpgradeBehavior: install
# The setup's AppId (installer\EdgeDeck.iss) as Inno Setup names its uninstall entry.
ProductCode: '{F1EA497D-8306-49A6-A42A-9E36995720E8}_is1'
ReleaseDate: $ReleaseDate
Installers:
$($installers -join "`n")
ManifestType: installer
ManifestVersion: $schema
"@

Write-Manifest "$PackageIdentifier.locale.en-US.yaml" @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.$schema.schema.json
PackageIdentifier: $PackageIdentifier
PackageVersion: $Version
PackageLocale: en-US
Publisher: $owner
PublisherUrl: https://github.com/$owner
PublisherSupportUrl: $Homepage/issues
PackageName: EdgeDeck
PackageUrl: $Homepage
License: MIT
LicenseUrl: $Homepage/blob/v$Version/LICENSE
ShortDescription: Small tabs on the screen edge that open panels for quick actions, media, volume, brightness and monitors.
Description: |-
  EdgeDeck docks small tabs to the edge of the screen. Rest the pointer on one and a panel slides
  out: your own quick actions, recent files, media controls, volume, monitor brightness (DDC/CI),
  synced lyrics, or switching which monitors are on. Panels never take the keyboard focus, and
  EdgeDeck uses almost nothing while it waits: no polling, no background updates, native Win32.
Moniker: edgedeck
Tags:
- edge
- sidebar
- launcher
- media
- volume
- brightness
- monitors
- productivity
ReleaseNotesUrl: $Homepage/releases/tag/v$Version
ManifestType: defaultLocale
ManifestVersion: $schema
"@

Write-Host "Manifests: $dir"
$winget = Get-Command winget -ErrorAction SilentlyContinue
if ($winget) {
    & $winget.Source validate --manifest $dir
    if ($LASTEXITCODE -ne 0) { throw 'winget validate rejected the manifests.' }
} else {
    Write-Host 'winget is not installed here: the manifests were not validated.'
}

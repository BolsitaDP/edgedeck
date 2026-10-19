<#
.SYNOPSIS
    Fetches the Inno Setup that builds EdgeDeck's setup program, checks it, and installs it for the
    current user. Prints the path of ISCC.exe.

.DESCRIPTION
    One pinned version, so every setup program is built by the same compiler: the download is
    refused unless its SHA256 is the one written here and it carries a valid signature from
    Inno Setup's publisher. Nothing is installed for all users and nothing is added to the Start
    menu. CI runs this before scripts\installer.ps1; on your own machine, installing Inno Setup
    from its site works just as well.

.PARAMETER Destination
    Where Inno Setup goes. Default: %LOCALAPPDATA%\Programs\Inno Setup 6, where installer.ps1 looks.
#>
[CmdletBinding()]
param([string]$Destination = (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6'))

$ErrorActionPreference = 'Stop'
$version = '6.7.3'
$url = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe'
$sha256 = '9C73C3BAE7ED48D44112A0F48E66742C00090BDB5BEF71D9D3C056C66E97B732'
$publisher = 'Pyrsys B.V.' # Inno Setup's publisher, who signs its releases

$iscc = Join-Path $Destination 'ISCC.exe'
if (Test-Path -LiteralPath $iscc) {
    $installed = (Get-Item -LiteralPath $iscc).VersionInfo.ProductVersion
    if ($installed -and $installed.Trim().StartsWith($version)) { $iscc; return }
}

# A folder of its own for the download, emptied first: nothing else is run from it.
$download = Join-Path ([IO.Path]::GetTempPath()) ('innosetup-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $download | Out-Null
try {
    $setup = Join-Path $download "innosetup-$version.exe"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $url -OutFile $setup -UseBasicParsing

    $hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash
    if ($hash -ne $sha256) { throw "Inno Setup download has SHA256 $hash, expected $sha256. Not running it." }
    $signature = Get-AuthenticodeSignature -LiteralPath $setup
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike "*O=$publisher*") {
        throw "Inno Setup download is not validly signed by $publisher ($($signature.Status)). Not running it."
    }

    $process = Start-Process -FilePath $setup -PassThru -Wait -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS', "/DIR=`"$Destination`"")
    if ($process.ExitCode -ne 0) { throw "Inno Setup's installer failed ($($process.ExitCode))." }
    if (-not (Test-Path -LiteralPath $iscc)) { throw "Inno Setup installed, but $iscc is not there." }
    $iscc
} finally {
    Remove-Item -LiteralPath $download -Recurse -Force -ErrorAction SilentlyContinue
}

<#
.SYNOPSIS
    Writes a release's notes: its section of CHANGELOG.md, how to install, and the checksums.

.DESCRIPTION
    Takes the "## [<version>]" section of CHANGELOG.md - everything up to the next "## [" heading -
    and adds what someone landing on the release page needs: which zip to download, how to install
    it, and the SHA256 of every zip in -PackageDir.

    Fails when CHANGELOG.md has no section for the version, so a release cannot go out with notes
    nobody wrote. Before tagging vX.Y.Z, rename "## [Unreleased]" to "## [X.Y.Z] - YYYY-MM-DD".
    -Unreleased takes the "## [Unreleased]" section instead, for a rehearsal that publishes nothing.

.PARAMETER Version
    major.minor.patch, as in the tag without its "v".

.PARAMETER PackageDir
    Where package.ps1 put the zips and their .sha256 files (dist\ by default). Without any, the
    notes have no checksum list.

.PARAMETER Unreleased
    Use the "## [Unreleased]" section: what the next release would say, for a dry run.

.PARAMETER OutFile
    Where the notes are written. Without it they are printed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\release-notes.ps1 -Version 1.0.0 -OutFile dist\notes.md
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Version,
    [string]$PackageDir,
    [string]$OutFile,
    [switch]$Unreleased
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $PackageDir) { $PackageDir = Join-Path $repo 'dist' }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must be major.minor.patch, got '$Version'." }

$lines = [IO.File]::ReadAllLines((Join-Path $repo 'CHANGELOG.md'), [Text.Encoding]::UTF8)
$heading = if ($Unreleased) { 'Unreleased' } else { $Version }
$start = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match ('^## \[' + [regex]::Escape($heading) + '\]')) { $start = $i; break }
}
if ($start -lt 0) {
    throw "CHANGELOG.md has no '## [$Version]' section. Rename '## [Unreleased]' to '## [$Version] - <date>' before tagging."
}
$end = $lines.Count
for ($i = $start + 1; $i -lt $lines.Count; $i++) {
    # The next version's heading, or the link references Keep a Changelog puts at the bottom.
    if ($lines[$i] -match '^## \[' -or $lines[$i] -match '^\[[^\]]+\]:\s') { $end = $i; break }
}
$section = ($lines[($start + 1)..($end - 1)] -join "`n").Trim()
if (-not $section) { throw "The '## [$heading]' section of CHANGELOG.md is empty." }

$notes = @"
$section

## Install

Windows 10 (1809 or later) or Windows 11. Download ``EdgeDeck-$Version-win-x64.zip`` (or
``win-arm64`` for Windows on ARM), unzip it anywhere and double-click **Install.cmd**. It installs
for your user only, without administrator rights, and appears in Settings > Apps > Installed apps.

EdgeDeck is not code-signed yet, so SmartScreen may warn about an unknown publisher. Compare the
download with the checksums below.
"@

# CHANGELOG.md is wrapped at 100 columns, and a release page shows every line break it is given:
# the lines of a paragraph or of a list item are joined back into one. Headings, list items, tables
# and code blocks start lines of their own.
function Join-WrappedLines([string]$text) {
    $out = New-Object System.Collections.Generic.List[string]
    $inFence = $false
    foreach ($line in ($text -split "`n")) {
        if ($line -match '^\s*```') { $inFence = -not $inFence; $out.Add($line); continue }
        $previous = if ($out.Count) { $out[$out.Count - 1] } else { '' }
        $startsBlock = $line -match '^\s*(#|[-*+] |\d+\. |\||>)' -or -not $line.Trim()
        $previousEnds = -not $previous.Trim() -or $previous -match '^\s*(#|\||```)'
        if (-not $inFence -and -not $startsBlock -and -not $previousEnds) {
            $out[$out.Count - 1] = $previous.TrimEnd() + ' ' + $line.Trim()
        } else {
            $out.Add($line)
        }
    }
    return ($out -join "`n")
}
$notes = Join-WrappedLines $notes

$sums = @(Get-ChildItem -Path $PackageDir -Filter "EdgeDeck-$Version-*.zip.sha256" -ErrorAction SilentlyContinue | Sort-Object Name)
if ($sums) {
    $notes += "`n`n## SHA256`n`n" + '```' + "`n"
    foreach ($s in $sums) { $notes += (Get-Content -LiteralPath $s.FullName -Raw).Trim() + "`n" }
    $notes += '```'
}

if ($OutFile) {
    [IO.File]::WriteAllText($OutFile, $notes + "`n", (New-Object Text.UTF8Encoding $false))
    Write-Host "Notes: $OutFile"
} else {
    $notes
}

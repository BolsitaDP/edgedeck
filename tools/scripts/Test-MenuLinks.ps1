<#
.SYNOPSIS
    The two menu commands that leave EdgeDeck: "Check for updates..." opens the project's releases,
    and "Report a problem..." opens a new bug report and puts the end of the log on the clipboard
    with the user's own folder masked.

.DESCRIPTION
    Runs a test copy (apptest.exe, scratch config) and gives it the commands through its trigger
    folder. What the browser opened is read from its windows' titles only - nothing of the page is
    captured. A line with this user's profile path is added to the test copy's log first, so the
    clipboard can be checked for the mask.

    OPENS TWO TABS IN YOUR DEFAULT BROWSER (close them afterwards) and uses the clipboard: what was
    on it as text is put back at the end.
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

# The titles of every top-level window that has one: where a browser says which page it shows.
if (-not ('Titles' -as [type])) {
    Add-Type -TypeDefinition @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class Titles {
    delegate bool Proc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(Proc p, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    public static List<string> All() {
        var list = new List<string>();
        EnumWindows((h, l) => { if (IsWindowVisible(h)) { var sb = new StringBuilder(512); GetWindowText(h, sb, 512); if (sb.Length > 0) list.Add(sb.ToString()); } return true; }, IntPtr.Zero);
        return list;
    }
}
"@
}
function Wait-Title($pattern, $seconds) {
    $found = $null
    [void](Wait-Until -TimeoutSeconds $seconds -PollMs 300 -Condition {
        $script:found = [Titles]::All() | Where-Object { $_ -match $pattern } | Select-Object -First 1
        [bool]$script:found
    })
    return $script:found
}

$savedClipboard = Get-Clipboard -Raw -ErrorAction SilentlyContinue
$app = Start-TestApp -Tabs QuickActions -Settings @('theme=Dark', 'fullscreenGuard=0')
try {
    # 1. Check for updates: the releases page.
    Set-Content -Path (Join-Path $app.Trigger 'updates.txt') -Value 'x'
    $title = Wait-Title 'Releases?.*edgedeck|edgedeck.*Releases?' 15
    Check '1. Check for updates opens the project''s releases in the browser' ([bool]$title) (([Titles]::All() | Select-Object -First 8) -join ' / ')
    Check '   and the log has no failure to open it' ((Get-Content $app.Log -Raw) -notmatch 'could not open')

    # 2. Report a problem: the bug form, and the log on the clipboard with the profile masked.
    $secret = Join-Path $env:USERPROFILE 'Documents\edgedeck-private-report-test.txt'
    Add-Content -Path $app.Log -Value "12:00:00 error Test: something about $secret"
    Set-Clipboard -Value 'before the report'
    Set-Content -Path (Join-Path $app.Trigger 'report.txt') -Value 'x'
    $title = Wait-Title 'New Issue|Sign in to GitHub|Iniciar sesi.n en GitHub|issues/new' 15
    Check '2. Report a problem opens a new issue (or GitHub''s sign-in on the way to it)' ([bool]$title) (([Titles]::All() | Select-Object -First 8) -join ' / ')
    Start-Sleep -Milliseconds 500
    $clip = Get-Clipboard -Raw
    Check '   the clipboard holds the end of the log' ($clip -match 'something about') $clip
    Check '   with the profile folder shown as %USERPROFILE%' ($clip -match [regex]::Escape('%USERPROFILE%\Documents\edgedeck-private-report-test.txt'))
    Check '   and the user''s own folder nowhere in it' (-not ($clip -match [regex]::Escape($env:USERPROFILE)))
    Check '   and the version line from this start' ($clip -match 'EdgeDeck \d+\.\d+\.\d+ \(x64\) started')
} finally {
    Stop-TestApp $app
    if ($null -ne $savedClipboard) { Set-Clipboard -Value $savedClipboard } else { Set-Clipboard -Value $null }
}

'Two browser tabs were opened by this test; close them when done.'
if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

<#
.SYNOPSIS
    Exercises the problem-report path: a notice in the panel header while the panel is open,
    a tray notification while it is closed.

.DESCRIPTION
    Starts a test copy of EdgeDeck (apptest.exe, scratch config) and injects problem reports
    into its tab the way a widget's ReportProblem would:

      1. a report while the panel is closed -> a Windows notification from the tray icon
      2. hover the tab, a report while it is open -> replaces the title in the header
      3. a long report -> must be clipped to the header, not painted over the rows
      4. six seconds later -> the title is back

    The checks it can make by itself: the panel stays open through the reports, the copy exits
    cleanly, and the log holds no "Notice:" error (that would mean the tray call failed).
    What the header and the notification LOOK like needs eyes: pass -ScreenshotDir and look at
    the pictures. The notification itself is not captured (it would mean photographing the
    desktop); expect to see a toast.

    Moves the real cursor and may pop a toast. A second copy of EdgeDeck finds the real one's
    global hotkeys taken, so a "shortcut already taken" toast at startup is normal, not a fault.

.PARAMETER ScreenshotDir
    Save pictures of the panel (cropped, over a plain backdrop) into this folder.
#>
[CmdletBinding()]
param([string]$ToolsDir, [string]$ScreenshotDir)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\UiHelpers.ps1"

if ($ScreenshotDir) { New-Item -ItemType Directory -Force -Path $ScreenshotDir | Out-Null }
$primary = Get-PrimaryRect
$failures = 0
function Check($label, $ok) {
    '{0} {1}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $label
    if (-not $ok) { $script:failures++ }
}

# A plain block behind where the tab and its panel will be, so a picture of the panel holds
# nothing else. Started BEFORE the app: the newest topmost window is on top, and a backdrop
# created afterwards would cover the tab and swallow the hover.
$backdrop = Start-Backdrop -X ($primary.R - 460) -Y ($primary.B / 2 - 220) -Width 460 -Height 440 -Seconds 90 -ToolsDir $ToolsDir
$app = Start-TestApp -Tabs QuickActions
try {
    $tab = (Get-TestTabs $app)[0]

    function Shot($name) {
        if (-not $ScreenshotDir) { return }
        $panel = Get-TestPanel $app
        if ($panel) { Take-Shot ($panel.L - 10) ($panel.T - 10) ($panel.R - $panel.L + 20) ($panel.B - $panel.T + 20) (Join-Path $ScreenshotDir "notice-$name.png") }
    }
    function Hover { Move-Mouse ($primary.R / 2) ($primary.B / 2); Start-Sleep -Milliseconds 500
                     Move-Mouse (($tab.L + $tab.R) / 2) (($tab.T + $tab.B) / 2); Start-Sleep -Milliseconds 1300 }

    # 1. closed panel -> tray notification
    Send-TestApp $app 'Windows could not complete this action.'
    Start-Sleep -Seconds 2
    Check 'closed panel: the tray notification call raised no error' (-not (Select-String -Path $app.Log -Pattern 'Notice:' -Quiet))

    # 2-4. open panel -> header
    Hover
    Shot 'title'
    Check 'hover opens the panel' ([bool](Get-TestPanel $app))
    Send-TestApp $app 'Windows could not change the display layout (error 87).'
    Start-Sleep -Milliseconds 600
    Shot 'report'
    Check 'a report does not close the open panel' ([bool](Get-TestPanel $app))
    Send-TestApp $app 'This is a deliberately long message that goes on and on, to check that anything past the second line is cut off instead of being painted over the rows below it.'
    Start-Sleep -Milliseconds 600
    Shot 'long'
    Start-Sleep -Milliseconds 6800
    Shot 'after'
    Check 'the panel is still open when the report times out' ([bool](Get-TestPanel $app))

    Move-Mouse ($primary.R / 2) ($primary.B / 2); Start-Sleep -Milliseconds 1000
    Check 'leaving the tab closes the panel' (-not (Get-TestPanel $app))
    Check 'the log holds no tray-notification error' (-not (Select-String -Path $app.Log -Pattern 'Notice:' -Quiet))
} finally {
    Stop-TestApp $app -KeepScratch
    if ($backdrop) { Stop-Process -Id $backdrop.Id -Force -ErrorAction SilentlyContinue }
}
Check 'the test copy exited cleanly' $app.Process.HasExited
Remove-Item $app.Scratch -Recurse -Force -ErrorAction SilentlyContinue

if ($failures) { Write-Host "$failures check(s) failed."; exit 1 }
Write-Host 'All checks passed.'

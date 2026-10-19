# Desktop test tools

What the unit tests (`tests/`) cannot cover: windows, the pointer, screens, DPI. These are developer
tools, not part of the product. Each exists because it found a real bug: buttons covered by a label,
controls overlapping at some scales, a panel that would not close after a monitor switch, a notice
lost with a null recipient.

> **Read this before running anything.** The scripts **move the real cursor, press real keys and
> open windows on your primary monitor**, and some **switch monitors off and on**. The configurations
> they use are isolated (they never touch your `config.txt` or your log), but the desktop is yours:
> run them when you can leave the machine alone for a few minutes. The ones marked 🖥️ switch
> monitors and refuse to run without `-AllowMonitorSwitching`.

## Building

The tools are not built by default. In a folder of their own, to keep them apart from the normal
build:

```powershell
cmake -S . -B build-tools -G "Visual Studio 17 2022" -A x64 -DEDGEDECK_BUILD_TOOLS=ON -DEDGEDECK_BUILD_TESTS=OFF
cmake --build build-tools --config Release
```

They end up in `build-tools\tools\Release\`. Scripts look for them there, then in
`build\tools\Release\`, or wherever `-ToolsDir` (every script has it) or the `EDGEDECK_TOOLS_DIR`
environment variable says. CI **builds** them but does not run them - they need a desktop session;
building them is what stops them rotting when the code they call changes.

Run them from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File tools\scripts\Test-SettingsLayout.ps1
```

## The programs

| Program | What it is |
|---|---|
| `dispcfg` | Lists which monitors are on (with their EDID id and the GDI device `gdi=\\.\DISPLAY2` they are drawn through, which is what ties an id to a screen rectangle), marks Windows' primary and the "main" one the main-monitor-only button would use (`<== primary`, `<== main`), validates or applies a layout (`main`, `tv`, `all`, `only`), and saves and restores a snapshot. Uses the same `DisplayTopology` as the Displays tab, so what it says holds for the app. |
| `settingsharness` | Opens **only** the Settings window (no tabs, no shortcuts, no single-instance lock) and prints a layout report: controls outside the window, overlapping controls, text wider than its control. `dpi=N` sends the `WM_DPICHANGED` a monitor at that scale would, without touching your display settings. `drop=N` opens the Nth combo box's list. `lang=es` shows the window in that language. `window=actions` hosts the quick-actions editor instead, `window=shortcuts` the shortcuts window and `window=layouts` the display layouts window. `dropfiles=X,Y,path\|path` drags those files onto the point (X,Y) of the window - a real OLE drag with the data object Explorer gives - and prints what came of it (the editor's list, status line and what Save would keep; on Settings, the Recent folder box); `ole` starts OLE before the controls are made. |
| `apptest` | The **real** `App` in another process, without the single-instance lock, so it runs next to your EdgeDeck. It reads `trig*.txt` files from a folder and hands them to the first tab as `PanelWidget::ReportProblem` would; `quit.txt` closes it. It installs the crash handler as `WinMain` does and can be crashed on purpose: `crash.txt` causes a real access violation and `terminate.txt` an unhandled exception. And the menu commands that have no window to click: `pause.txt`, `restart.txt` and `editor.txt` (open the actions editor). |
| `backdrop` | A plain always-on-top window, so that a capture of whatever is in front of it holds nothing of your desktop. |
| `fullscreenwin` | A borderless window that comes to the foreground, to pose as a full-screen game. Deliberately not topmost: the tabs are, and must stay above it. |

## The scripts

Every script exits non-zero when a check fails.

| Script | What it checks | What it does to your machine |
|---|---|---|
| `Test-SettingsLayout.ps1` | The Settings layout - or, with `-Dialog actions`, `shortcuts` or `layouts`, that window's - from 100% to 250% (`-Dpis 96,144`), in a language (`-Language en\|es`), with `-ScreenshotDir` for pictures. Long text meant to wrap is not "too wide"; it is a problem if, wrapped, it does not fit its box. | Opens a window for a few seconds. |
| `Test-FullscreenGuard.ps1` | Over a full-screen app the tab is hidden and nothing opens; it comes back when the app is gone; with the guard off, or under an ordinary maximized window, the tab stays and opens. | A dark window covers the primary monitor for ~10 s; moves the cursor. |
| `Test-QuickActions.ps1` | The Quick Actions panel: height by rows (exactly one row taller per row added), pressing a row starts what it names and only that, a row that cannot open leaves a notice in the header (with a control: no pixel of that colour before), an accented name is a real row, and each row shows the icon of what it opens - coloured pixels in the slot for a program and a folder, none (a grey lettered badge) for a missing program. | Moves the cursor and clicks in the test copy's panel; starts (and closes) small plain windows. |
| `Test-ActionDrop.ps1` | Dragging files from Explorer onto the actions editor: it lands on every kind of control (the list, the three edit boxes - also with OLE started before they were made -, a label, a button, the empty edge), a file is named without its extension and a folder keeps its whole name, the dropped path is what is saved, the drag ends as a link (nothing copied or moved), a long name is cut to 40, and what does not fit (12 rows) is counted in the status line. | Moves the cursor and opens the editor for a few seconds per case. Creates and deletes files in `%TEMP%`. |
| `Test-RecentFiles.ps1` | The Recent files tab: height by files (hidden files, partial downloads and folders left out), a new file appears when the panel is opened again (exactly one row taller), a program has a picture and a document a badge (how much of the square is one colour), pressing a file opens it (a shortcut to the plain window, seen by its command line), a type with no program leaves a notice and does **not** open Windows' Open-with window, and the short panels for a missing and an empty folder. | Moves the cursor and clicks in the test copy's panel; creates and deletes files in `%TEMP%`; starts (and closes) a small plain window. |
| `Test-OpenDelayPause.ps1` | The open delay (a pointer that leaves early opens nothing; one that stays opens neither early nor much later, with a no-delay control), pause (hides the tab and the pointer opens nothing; resume brings it back) and Restart (the old copy exits, the new one takes over with the same arguments, without calling it a crash and without two left running). | Moves the cursor. Starts and stops its own `apptest` copies. |
| `New-AppIcon.ps1` | **Not a test: it generates `res\EdgeDeck.ico`** (9 sizes) from a vector description, and with `-Preview` a sheet to look at it on dark and light. Run it again after changing the drawing, then build. | Writes the repository's `.ico`. |
| `Test-PanelPaint.ps1` | The panel is **painted** - not just "visible" to Windows - as soon as the pointer rests on the tab, during the slide, and when opened a second time, without the pointer having to enter it. Looks at pixels: a plain window where the panel will open, and more than one colour in the picture is the panel. | Moves the cursor; puts a plain window on each edge of the primary monitor for ~15 s. |
| `Test-MoveHandle.ps1` | Holding a panel's move handle and dragging moves the tab and the panel together along their edge, they stop at the monitor's top, a click without a move saves nothing, and where the tab is let go is saved to the pixel: a restart on that config puts it at the same top. | Moves the cursor and holds the left button for a few seconds. |
| `Test-TabPlacement.ps1` | Each tab docks to its configured edge and monitor (right and left, the primary and another, and a disconnected one that falls back to the primary), its panel opens next to it and inside its monitor, and - with two adjacent monitors - the sliding panel is **not seen on the neighbour**: a plain window on that strip is captured ~460 times while the panel slides and the foreign pixels are counted, with a control (an orange window the scan must see) so that "0 foreign pixels" means something. | Moves the cursor; puts plain windows on the neighbouring monitor for ~10 s. Does not switch monitors; skips the two-monitor part with one monitor. |
| `Test-Notices.ps1` | The problem notice in the panel header and from the tray (`-ScreenshotDir` to see them). | Moves the cursor; a notification may appear. |
| `Capture-TabIcons.ps1` | A picture of every tab's icon, enlarged (`-Path`, `-Zoom`). | Puts a plain strip on the right edge for a few seconds. |
| `Measure-Perf.ps1` | Memory, threads, handles and CPU of two builds, alternated (`-ExeA`, `-ExeB`, `-Runs`). | Moves the cursor ~20 s per run. **Needs the real EdgeDeck closed** (`-StopRunningInstance`). |
| `Test-DisplaySwitching.ps1` 🖥️ | `dispcfg` switches layouts and back. By default it only turns the TV off and on. | **Switches monitors.** Saves a snapshot and leaves the starting layout. |
| `Test-CrashHandling.ps1` | Crashes a test copy for real and checks what it leaves: a log line, a valid minidump and **one** restart (none if the restarted copy crashes at once, or with `restartAfterCrash=0`). | Starts and kills its own `apptest.exe` processes. |
| `Test-DisplayHotkeys.ps1` 🖥️ | The `Ctrl+Alt+Shift+1`..`4` shortcuts with the panel closed (written for the four layouts carried over from the old fixed buttons). | **Switches monitors and presses keys.** Needs the real EdgeDeck closed, since it owns the shortcuts. |

`UiHelpers.ps1` is the shared library: windows found **by process**, mouse, keyboard, captures, and
`Start-TestApp` to start a test copy with its own configuration.

To reopen your EdgeDeck after a script closed it: `scripts\install.ps1`.

## How not to get it wrong

- **Your own EdgeDeck's tabs are on the same screen.** The test copy's and yours share the right
  edge. Scripts pick theirs **by process** (`Get-Wins -ProcessId`), never "the first visible one": that
  guess went wrong whenever the window order changed.
- **The plain background goes first.** Among always-on-top windows the newest wins: a `backdrop`
  created after the app covers the tab and swallows the hover.
- **Captures.** `Take-Shot` copies whatever is on that screen rectangle. Point it only at a window
  standing on a `backdrop`, or at a window cropped to its visible bounds (`Get-VisibleRect`, which
  leaves out the invisible border). A capture with someone else's content in it is deleted.
- **"Visible" is not "painted".** `IsWindowVisible` is true for a per-pixel-alpha window nobody has
  given a picture yet. The window-based scripts passed for weeks while the panel opened **blank**: its
  first picture was presented with the window off the desktop, where DWM does not keep it. Only a
  pixel check sees that: `Test-PanelPaint`.
- **`[math]::Min(1, 0.63)` returns 1.** Given an integer and a decimal, PowerShell picks the integer
  overload and rounds. `Test-TabPlacement` once put a tab at the very bottom that way, and its pixel
  check passed seeing nothing. It was found by disabling the fix on purpose to watch the test fail -
  and it did not fail. A test that has not been seen failing has proved nothing; use `1.0` and `0.0`.
- **`powershell -File` and lists.** A comma-separated list arrives as one piece of text, so an
  `[int[]]` parameter would read `96,144` as 96144. Scripts that take lists take text and split it.
- **PowerShell variables are case-insensitive.** `$tab` and a `$Tab` parameter are the same variable.
- **An environment problem that looked like a bug.** The "maximised" case of `Test-FullscreenGuard`
  failed on a machine with no taskbar on the primary monitor: there the work area *is* the monitor,
  and a borderless window that size rightly counts as full screen. The case now uses the work area
  minus a title bar, which is what a real maximised window has.

## Adding a scenario

1. A `Test-<Something>.ps1` script with help at the top (`.SYNOPSIS`, `.DESCRIPTION`, and a visible
   warning if it does more than open a window).
2. `. "$PSScriptRoot\UiHelpers.ps1"`, and `Start-TestApp` / `Stop-TestApp` (in `try`/`finally`) for the
   test copy; `Send-TestApp` to inject a notice.
3. If it needs something from the code no tool exposes, extend `apptest` or `settingsharness` rather
   than building another: they share the product's sources.
4. A script that switches monitors or closes the real EdgeDeck must **refuse** without an explicit
   parameter, and leave the desktop as it found it even when it fails (`finally`).

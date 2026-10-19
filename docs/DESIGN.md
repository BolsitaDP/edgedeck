# EdgeDeck design notes

Why EdgeDeck is built the way it is. Most of these are decisions that look arbitrary until you
know what went wrong the other way; several are bugs that are now covered by a test. Read the
relevant section before changing that part of the code.

## Ground rules

- **Nothing runs while nothing happens.** No render loop, no mouse polling, no global hooks. Hover
  uses `TrackMouseEvent` / `WM_MOUSELEAVE`; changes in the system (media sessions, audio devices,
  monitors, theme) arrive as events. There are timers in four places only, and none runs while the
  panels are closed: the slide animation (15 ms while it lasts), the short wait before closing
  (320 ms), a 500 ms tick *only while a panel is visible* and only for the widgets that need the
  playback position (Media, Lyrics - Windows has no event for it), and the 6 s a problem notice
  stays in a panel header.
- **Never take the focus.** Panels are `WS_EX_NOACTIVATE`, and nothing a panel does may put a window
  in front of the user: a problem is reported in the panel header, or as a notification from the
  tray icon when no panel is on screen (`PanelWidget::ReportProblem`). Modal `MessageBox`es used to
  freeze whatever the user was doing; the three left in `main.cpp` are fatal start-up failures.
- **Measure what a feature costs.** Working set, private bytes, handles and threads against the
  previous build, both idle and after a panel has opened once - a feature can leave idle untouched
  and still cost megabytes the first time it is used (see *Icons* below).
- **Work that can block happens off the UI thread,** starts only from an explicit user action
  (opening a panel, pressing a button), and answers with one posted message.

## Asynchronous results

Background work (WinRT media sessions, DDC/CI, HTTPS, folder listings, icon lookups) hands its
result to the UI thread as the `WPARAM` of a message in the widget's private `WM_APP` range, inside
one envelope type (`AsyncResult.h`) that carries a request id and a kind. The receiver always frees
the envelope, whether it asked for it or not, and drops it unless the id is one it has in flight.
That removes the three ways this breaks: a leak when the tab was destroyed mid-job, a leak when the
message reaches a recycled `HWND`, and stale data when a newer refresh superseded an older one.
`PostOrDelete` frees the envelope when the window is already gone.

## Rendering

**Per-pixel alpha.** Each window's shape is its alpha channel: Direct2D draws into a premultiplied
32-bit DIB through a *software* `ID2D1DCRenderTarget`, and `UpdateLayeredWindow` hands it to the
compositor (`LayeredTarget`). The corners are antialiased at every scale; the earlier `SetWindowRgn`
clip is a 1-bit mask and was jagged from 125% up. The surface is cleared to transparent and the
rounded rectangle filled on top - clearing to the opaque panel colour would make every pixel opaque.
An earlier attempt concluded that "a D2D surface without a DXGI device loses alpha"; that holds for
`ID2D1HwndRenderTarget`, which composites through GDI, but not for a DC target bound to a DIB. So
there is no Direct3D, no GPU readback, no driver load for a 300x200 window. Measured with two tabs
against the region path: 19.6 MB working set instead of 34.5, 5 MB private instead of 42, 13 threads
instead of 24, 200 handles instead of 334. If the DC target cannot be created, a window falls back to
the region path; the choice is made once in `AttachToWindow`. Text is grayscale, not ClearType: a
surface with alpha does not know what is behind it.

**The panel must be painted where DWM can see it.** The slide starts with the panel outside the
desktop, and DWM does not keep the picture of a per-pixel-alpha window presented there: the first
paint was lost and the panel stayed blank - slide included - until the pointer entered it. It is
painted again on the first frame that touches its monitor (`m_panelPaintedOnScreen`). Window-based
checks ("does it exist, is it where it should be") passed throughout; `Test-PanelPaint` looks at
pixels.

**A neighbouring monitor.** When another monitor sits right across the panel's edge, the panel would
be seen coming out of it during the 200 ms slide. For that case only, the panel carries a window
region clipping it to its own monitor while it slides. Order matters: moving in, the window moves and
then the region is set; moving out, the region is set and then the window moves - a window and its
region do not change in the same frame, and the frame in between must not show anything on the
neighbour (a first version did, in about 1 frame in 460, and the pixel test caught it).

**Transforms.** `SetTransform` replaces the matrix rather than composing it, and the panel chrome
lives in a translation installed before the widget draws. `Renderer` composes every local transform
on top of the current one (`ScopedTransform::Compose`); doing it the other way sends icons out of the
panel, and ending with `SetTransform(Identity)` instead of restoring shifts the rest of the content
by exactly the chrome height.

**Theme.** The palette uses Windows' own greys (`#202020`, `#1A1A1A`, `#333333`, and `#F3F3F3` for
light) and the user's accent (`Themes\Personalize\AccentColor`), pushed towards white or black until
it has 3:1 contrast with the panel. High contrast takes over all colours. The palette is recomputed
on `WM_SETTINGCHANGE` / `WM_THEMECHANGED` / `WM_SYSCOLORCHANGE` and cached; it used to be read on
every paint - a registry read and a `SystemParametersInfo`, some fifteen times per animation frame.

**Icons.** Tab icons are code points of the system icon font (Segoe Fluent Icons, or Segoe MDL2
Assets), with a plain text glyph as fallback (`TabIcon` / `TabGlyph`). Row pictures (Quick Actions,
Recent files) are read straight from the file - `PrivateExtractIcons` on a program, `imageres.dll`
id 3 for a folder, and a `.lnk` parsed by hand (MS-SHLLINK: icon location, LinkInfo, the
environment-variable block, the relative path; bounds-checked and fuzzed in the tests). The obvious
route - `SHGetFileInfo`, the system image list, `IShellLink`, `AssocQueryString` - costs a process
about 8 MB, ~100 handles and two threads the first time and gives none of it back; the file read
costs about 0.4 MB. A row with nothing honest to show (a document, a web address, a Store app) gets a
lettered badge. An icon is drawn once over black and once over white, which gives each pixel's alpha
exactly whatever kind of icon it is (`ActionIcons::SeparateFromBackgrounds`).

## The tabs

**Hover.** The panel opens 5 logical pixels from its tab so the two read as separate shapes; the gap
belongs to neither window, but crossing it takes milliseconds and the 320 ms close delay absorbs it.
An optional open delay (`openDelayMs`) arms a one-shot timer only between reaching a closed tab and
either leaving or the delay running out.

**Full-screen apps.** The tabs live on an edge the pointer constantly hits - in a game it is pushed
against it - so a panel jumping over the game is the surprise. If the foreground window covers the
tab's monitor edge to edge, a hover does not open the panel. It is asked only at hover time; the
client area is compared with the monitor (a normal maximised window leaves the taskbar and does not
count; a borderless game does, even with its title bar pushed off screen); only *that* monitor counts;
the desktop and taskbar are ignored. Keyboard shortcuts always open.

**Monitor and edge.** Monitors are identified by their EDID id (`GSM7768`), not by adapter LUID or
target id, which change across reboots and driver reloads. A tab whose monitor is off is not an
error: it docks to the primary monitor, keeps its setting, and goes back on `WM_DISPLAYCHANGE`. Only
left and right edges exist; top and bottom would need the tab and panel rotated.

**Pause.** Hides the tabs and closes every panel, pinned ones included, until resumed. Not
remembered across a restart, where it would look like a broken app. Shortcuts keep working.

## Opening things

Quick Actions and Recent files open through one launcher (`Actions`). `ShellExecuteEx` runs with
`SEE_MASK_FLAG_NO_UI`, and a failure becomes a sentence in the panel header. That flag does *not*
stop Windows' "How do you want to open this file?" window for a type with no program, and that window
takes the focus; so the launcher asks first: when `AssocQueryString(ASSOCSTR_EXECUTABLE)` answers
`OpenWith.exe`, the panel says "nothing is set to open that kind of file" instead. A lookup that
merely fails says nothing either way - shortcuts and the types Store apps open fail it, with
`ERROR_NO_ASSOCIATION` even, and open perfectly well (a first version treated that as "nothing opens
it"; a regression test holds it now). `Actions::Open` passes a real file name as it is;
`Actions::Run` expands `%variables%` in what a user typed.

Dropping files on the actions editor uses one OLE drop target on the window: OLE falls back to the
parent for a child control without one, which was checked on every kind of control rather than
assumed. `DragAcceptFiles` does not do that for child controls.

## Widgets

**Media and Lyrics** subscribe to `GlobalSystemMediaTransportControlsSessionManager` events
(`CurrentSessionChanged`, `SessionsChanged`) and each session's (`PlaybackInfoChanged`,
`MediaPropertiesChanged`), so starting, pausing or skipping updates an open panel at once.

**Brightness** shows the value live while dragging, but writes DDC/CI once, on release: several
monitors keep brightness in non-volatile memory. A rejected write puts the row back to the last value
the hardware confirmed. Core Audio volume is an in-process call, so the volume slider writes on every
move and the panel re-reads while open, to follow volume keys pressed elsewhere.

**Lyrics** come from LRCLIB over one shared WinHTTP session with keep-alive and explicit timeouts
(5 s resolve/connect/send, 8 s receive - WinHTTP's defaults are about 60 s, enough to hold a pool
thread a minute), a 4 MB response cap, and a local cache (`lyrics_cache`); "no lyrics" is cached for
7 days so a free public service is not asked again at every panel open.

**Displays.** Turning a monitor on is `SetDisplayConfig`, which Windows holds while the panels
resync - seconds, with a TV in the chain - so it runs on the thread pool. Turning monitors off
rewrites the current layout without them, so the others do not move; turning one on passes Windows
only the paths and lets it fill in modes and positions from what it remembers for that exact set of
monitors. The planner (`DisplayTopology`) has no windows and is tested against hand-made topologies;
`SDC_VALIDATE` checks a plan against the hardware without applying it. A shortcut always reads the
monitors again before acting - the panel may not have been opened for hours.

**Recent files** lists the folder when the panel opens, on a short-lived thread, and does not watch
it. Downloads is read from the registry (`User Shell Folders`), not asked of the shell.

## Start with Windows

The setting is the `EdgeDeck` value under `HKCU\...\CurrentVersion\Run`. Designed not to fail
silently:

- *On* means it will really start: Task Manager's Startup tab does not delete the entry, it writes a
  flag under `Explorer\StartupApproved\Run`. Reading only the entry made Settings say "on" while
  Windows ignored it.
- The registry is only written when the checkbox changes - saving from a test copy used to take the
  entry over.
- The result is read back after writing; a refusal becomes a notification.
- At start-up, an entry pointing at a file that no longer exists is repointed (at the installed
  copy, else the running one), without touching Task Manager's flag and without taking over an entry
  that points at *another copy that exists*.
- Once, from the installed copy, a missing entry is pointed out (`startupHintShown`).

**Packaged terminals.** Writes to `%LOCALAPPDATA%` and `HKCU` from inside an MSIX-packaged app (such
as some terminals) are silently redirected to a private store: an install "succeeds" and Windows
never sees it. `install.ps1` detects that (it writes a marker file and looks for it in the packages'
stores) and refuses; `-AllowSandbox` forces it, for testing the script only.

## Crashes

A resident utility that dies silently just disappears. `CrashHandler` is installed first thing in
`WinMain` and, on an unhandled exception or `std::terminate`, leaves:

- a log line - `Crash: access violation (0xC0000005) at EdgeDeck.exe+0x1A2B3C, thread 4812, up
  931 s, build 0x6702F1A4` (module offset and linker timestamp, enough to find the line with the
  `.pdb`);
- a minidump in `%LOCALAPPDATA%\EdgeDeck\crashes\` (about 80 KB, the newest 5 kept);
- one restart. A copy started by a crash that crashes again within a minute is not restarted, to
  avoid a loop on something that fails at start-up. `restartAfterCrash=0` turns it off.

Nothing in the handler allocates (the heap may be the thing that broke) and the log line is written
before the dump and the restart. Throwing from inside a `std::terminate` handler does not work - the
CRT fail-fasts first - so the handler builds the exception record itself. Stack is reserved so a stack
overflow can be reported. A real `__fastfail` bypasses every handler by design.

## Shortcuts

The system-wide shortcuts are `RegisterHotKey` registrations on a hidden window - not keyboard
hooks. Which ones, is a setting (`Shortcuts.h`): exit and pause are whole chords, opening a tab and
switching displays are modifiers plus a digit, and any can be off. Every shortcut needs Ctrl, Alt or
Win (a Shift+letter would take that letter from everything the user types), and a set where two
would be the same keys is refused before it is saved. They are registered after the settings are
read (and after the tabs: the display ones exist only while there is a Displays tab), and again
after every change; one another program owns is logged and named in the Shortcuts window, and a
taken exit shortcut is also said once at start-up, with the tray menu's Exit as the way out.

## Configuration

`config.txt` is UTF-8 (a BOM is tolerated), one `key=value` per line, in `[settings]`, `[tab]` and
`[action]` sections. Unknown keys are ignored, which is what makes adding one a compatible change;
every value is clamped on load exactly as the Settings window clamps it. It is written only on Save,
to a temporary file swapped in atomically.

## Translations

Every user-facing text lives in one table, `src/Strings.h`, with all its translations side by side;
a missing translation does not compile. Texts with values are printf formats, and a unit test
requires every translation to carry the English specifiers in the same order. The log is not
translated. See CONTRIBUTING.md for adding a language.

## Tests

`tests/` covers everything that has no window: the LRC parser, the config file, the Displays
planner, notices between widget and tab, the startup-entry logic (against a scratch registry key),
crash-handler formatting and restart rules, tab and panel placement, the full-screen geometry, the
per-pixel-alpha surface (by reading the DIB), the icon in the built executable, quick actions, icon
extraction and shortcut parsing, the Recent files listing, and the string table. They run in CI
against a scratch `%LOCALAPPDATA%`.

Windows, the pointer and the screen are covered by the desktop tools in `tools/` - built in CI but
not run there, because they move the real cursor and some switch monitors. A test that has not been
seen failing has not proved anything: the habit here is to break the fix on purpose and watch the
test fail before trusting it.

#pragma once

#include <windows.h>

// Whether a full-screen app is in front of a monitor. Used to keep a tab's panel
// from popping open over a game or a video just because the pointer brushed the
// screen edge the tabs live on.
//
// Asked once, at the moment of the hover - there is no polling and no hook, so an
// idle EdgeDeck still costs nothing.
namespace Fullscreen {

// True if `clientArea` (a window's client rectangle, in screen coordinates) reaches
// every edge of `monitor`. The *client* area is what is compared, not the window
// rectangle and not the window style: a normal maximized window leaves the taskbar
// uncovered (or one pixel of it, with the taskbar hidden), so it never counts, while
// a borderless game does - including the ones that keep a caption but push it
// off-screen, which a style check would miss.
bool CoversMonitor(const RECT& clientArea, const RECT& monitor);

// True if the foreground window is an app covering `monitor` edge to edge. The
// desktop and the taskbar also cover a monitor and are not apps, so they are
// skipped, as is a minimised or cloaked window and one on another monitor: a video
// full-screen on the TV must not stop the panel opening on the monitor being used.
bool AppOnMonitor(HMONITOR monitor);

} // namespace Fullscreen

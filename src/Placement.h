#pragma once

#include <windows.h>

// Where a tab and its panel go on a monitor. Pure arithmetic on rectangles, so it is covered
// by the unit tests with the coordinates a multi-monitor desk really has (a monitor to the
// left of the origin, one above it) instead of needing a desktop to look at.
//
// All coordinates are physical pixels in virtual-screen space.

enum class ScreenEdge : int {
    Right = 0, // the default, and where every tab lived before the edge could be chosen
    Left = 1,
};

namespace Placement {

struct Input {
    RECT monitor{};            // the whole monitor, not the work area: the tabs overlay the taskbar
    ScreenEdge edge = ScreenEdge::Right;
    int tabWidth = 0;
    int tabHeight = 0;
    int panelWidth = 0;
    int panelHeight = 0;
    float verticalRatio = 0.5f; // 0 = top of the monitor, 1 = bottom
};

struct Result {
    RECT tab{};
    // The panel's left edge when it is open (beside the tab, on the side facing the screen) and
    // when it is closed (completely beyond the edge, where the slide starts and ends).
    int panelOpenX = 0;
    int panelClosedX = 0;
    int panelY = 0;
};

Result Compute(const Input& in);

// The panel's top, so that it is centred on the tab but never leaves the monitor. A panel taller
// than the monitor is pinned to the top rather than asking for a clamp with an empty range.
int PanelY(const RECT& tab, int panelHeight, const RECT& monitor);

// The part of a panel window (at x, y, size w by h) that lies on the monitor, in the window's own
// coordinates. Empty when none of it does. Used to keep a sliding panel from showing on a
// neighbouring monitor while it is still outside the edge it slides in from.
RECT VisiblePart(int x, int y, int w, int h, const RECT& monitor);

} // namespace Placement

// Covers the geometry that decides "a full-screen app is in front of this monitor". The
// part that asks Windows for the foreground window needs a desktop; what can go wrong
// without one is the comparison itself - which of a maximized window, a borderless
// game and the desktop count - so that is what is checked, with the coordinates a
// multi-monitor desk really produces (a monitor to the left of the origin, one above).
#include "TestHarness.h"

#include "Fullscreen.h"

namespace {

// The user's layout: an ultrawide at the origin, a 1080p monitor to its left, a 4K TV above.
constexpr RECT kUltrawide{0, 0, 3440, 1440};
constexpr RECT kLeftMonitor{-1920, 352, 0, 1432};
constexpr RECT kTvAbove{0, -2160, 3840, 0};

void TestExactCoverCounts() {
    TEST("Fullscreen: a client area reaching every edge of the monitor counts");
    CHECK(Fullscreen::CoversMonitor(kUltrawide, kUltrawide));
    CHECK(Fullscreen::CoversMonitor(kLeftMonitor, kLeftMonitor));
    CHECK(Fullscreen::CoversMonitor(kTvAbove, kTvAbove));
}

void TestOverhangCounts() {
    TEST("Fullscreen: a window that overhangs the monitor still counts");
    // Games that hide their caption by pushing it off-screen, and borderless windows a
    // few pixels larger than the display, have a client area bigger than the monitor.
    CHECK(Fullscreen::CoversMonitor(RECT{-8, -31, 3448, 1448}, kUltrawide));
    CHECK(Fullscreen::CoversMonitor(RECT{-1928, 321, 8, 1440}, kLeftMonitor));
}

void TestMaximizedWindowDoesNot() {
    TEST("Fullscreen: a normal maximized window does not - the taskbar is still uncovered");
    // Work area of the ultrawide with a 48 px taskbar along the bottom.
    CHECK(!Fullscreen::CoversMonitor(RECT{0, 0, 3440, 1392}, kUltrawide));
    // With the taskbar hidden Windows still leaves one pixel of it, so the pop-up works.
    CHECK(!Fullscreen::CoversMonitor(RECT{0, 0, 3440, 1439}, kUltrawide));
    CHECK(!Fullscreen::CoversMonitor(RECT{0, 1, 3440, 1440}, kUltrawide));
    CHECK(!Fullscreen::CoversMonitor(RECT{1, 0, 3440, 1440}, kUltrawide));
    CHECK(!Fullscreen::CoversMonitor(RECT{0, 0, 3439, 1440}, kUltrawide));
}

void TestOtherMonitorsDoNotCount() {
    TEST("Fullscreen: covering a different monitor is not covering this one");
    // A video full-screen on the TV must not stop the panel opening on the ultrawide.
    CHECK(!Fullscreen::CoversMonitor(kTvAbove, kUltrawide));
    CHECK(!Fullscreen::CoversMonitor(kLeftMonitor, kUltrawide));
    CHECK(!Fullscreen::CoversMonitor(kUltrawide, kTvAbove));
}

void TestSmallWindowDoesNot() {
    TEST("Fullscreen: an ordinary window does not");
    CHECK(!Fullscreen::CoversMonitor(RECT{100, 100, 1100, 800}, kUltrawide));
    CHECK(!Fullscreen::CoversMonitor(RECT{0, 0, 0, 0}, kUltrawide));
}

void TestDegenerateMonitor() {
    TEST("Fullscreen: an empty monitor rectangle is never 'covered'");
    // Everything covers nothing, which would otherwise make every window "full-screen".
    CHECK(!Fullscreen::CoversMonitor(kUltrawide, RECT{0, 0, 0, 0}));
    CHECK(!Fullscreen::CoversMonitor(kUltrawide, RECT{10, 10, 10, 500}));
    CHECK(!Fullscreen::CoversMonitor(kUltrawide, RECT{500, 500, 10, 10}));
}

} // namespace

void RunFullscreenTests() {
    TestExactCoverCounts();
    TestOverhangCounts();
    TestMaximizedWindowDoesNot();
    TestOtherMonitorsDoNotCount();
    TestSmallWindowDoesNot();
    TestDegenerateMonitor();
}

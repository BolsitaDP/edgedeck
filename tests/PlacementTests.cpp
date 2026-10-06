// Covers where a tab and its panel go on a monitor, for either edge, with the coordinates a
// multi-monitor desk really produces: a monitor to the left of the origin, one above it.
#include "TestHarness.h"

#include "Placement.h"

namespace {

// The user's layout: an ultrawide at the origin, a 1080p monitor to its left, a 4K TV above.
constexpr RECT kUltrawide{0, 0, 3440, 1440};
constexpr RECT kLeftMonitor{-1920, 352, 0, 1432};
constexpr RECT kTvAbove{0, -2160, 3840, 0};

Placement::Input Make(const RECT& monitor, ScreenEdge edge, float ratio = 0.5f) {
    Placement::Input in;
    in.monitor = monitor;
    in.edge = edge;
    in.tabWidth = 26;
    in.tabHeight = 76;
    in.panelWidth = 300;
    in.panelHeight = 200;
    in.verticalRatio = ratio;
    return in;
}

bool Same(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

void TestRightEdgeIsWhatItAlwaysWas() {
    TEST("Placement: the right edge keeps the layout every tab had before the edge was a choice");
    const Placement::Result r = Placement::Compute(Make(kUltrawide, ScreenEdge::Right));
    CHECK(Same(r.tab, RECT{3414, 682, 3440, 758}));
    CHECK_EQ(r.panelOpenX, 3114); // beside the tab, towards the screen
    CHECK_EQ(r.panelClosedX, 3440); // wholly beyond the edge
    CHECK_EQ(r.panelY, 620);        // centred on the tab
}

void TestLeftEdgeIsTheMirror() {
    TEST("Placement: the left edge mirrors it - tab flush left, panel opens to its right");
    const Placement::Result r = Placement::Compute(Make(kUltrawide, ScreenEdge::Left));
    CHECK(Same(r.tab, RECT{0, 682, 26, 758}));
    CHECK_EQ(r.panelOpenX, 26);
    CHECK_EQ(r.panelClosedX, -300);
    CHECK_EQ(r.panelY, 620);
}

void TestMonitorLeftOfTheOrigin() {
    TEST("Placement: a monitor at negative coordinates docks to its own edges, not the origin's");
    const Placement::Result right = Placement::Compute(Make(kLeftMonitor, ScreenEdge::Right, 0.0f));
    CHECK(Same(right.tab, RECT{-26, 352, 0, 428}));
    CHECK_EQ(right.panelOpenX, -326);
    CHECK_EQ(right.panelClosedX, 0); // which is the ultrawide's left edge: the slide needs clipping

    const Placement::Result left = Placement::Compute(Make(kLeftMonitor, ScreenEdge::Left, 1.0f));
    CHECK(Same(left.tab, RECT{-1920, 1356, -1894, 1432}));
    CHECK_EQ(left.panelOpenX, -1894);
    CHECK_EQ(left.panelClosedX, -2220);
}

void TestMonitorAboveTheOrigin() {
    TEST("Placement: a monitor above the origin has negative y and is laid out against its own top");
    const Placement::Result r = Placement::Compute(Make(kTvAbove, ScreenEdge::Right));
    CHECK_EQ(static_cast<int>(r.tab.top), -2160 + (2160 - 76) / 2);
    CHECK_EQ(static_cast<int>(r.tab.right), 3840);
    CHECK_EQ(r.panelOpenX, 3840 - 26 - 300);
}

void TestPanelStaysOnTheMonitor() {
    TEST("Placement: the panel is centred on the tab but never leaves the monitor");
    const Placement::Result top = Placement::Compute(Make(kUltrawide, ScreenEdge::Right, 0.0f));
    CHECK_EQ(top.panelY, 0); // centred it would start at -62
    const Placement::Result bottom = Placement::Compute(Make(kUltrawide, ScreenEdge::Right, 1.0f));
    CHECK_EQ(bottom.panelY, 1240); // 1440 - 200
    const Placement::Result onTv = Placement::Compute(Make(kTvAbove, ScreenEdge::Left, 0.0f));
    CHECK_EQ(onTv.panelY, -2160);
}

void TestPanelTallerThanTheMonitor() {
    TEST("Placement: a panel taller than the monitor is pinned to the top, not clamped on nothing");
    // std::clamp with an empty range is undefined; this used to be reachable with a tall widget on
    // a short monitor.
    Placement::Input in = Make(kUltrawide, ScreenEdge::Right);
    in.panelHeight = 2000;
    CHECK_EQ(Placement::Compute(in).panelY, 0);
    in.monitor = kLeftMonitor;
    CHECK_EQ(Placement::Compute(in).panelY, 352);
}

void TestVisiblePartOfASlidingPanel() {
    TEST("Placement: a sliding panel is cut to the monitor it belongs to");
    // Ultrawide, right edge: the panel slides in from x = 3440, over whatever is to the right.
    CHECK(Same(Placement::VisiblePart(3440, 620, 300, 200, kUltrawide), RECT{0, 0, 0, 0}));
    CHECK(Same(Placement::VisiblePart(3340, 620, 300, 200, kUltrawide), RECT{0, 0, 100, 200}));
    CHECK(Same(Placement::VisiblePart(3114, 620, 300, 200, kUltrawide), RECT{0, 0, 300, 200}));

    // Left edge: it slides in from x = -300, over whatever is to the left.
    CHECK(Same(Placement::VisiblePart(-300, 620, 300, 200, kUltrawide), RECT{0, 0, 0, 0}));
    CHECK(Same(Placement::VisiblePart(-100, 620, 300, 200, kUltrawide), RECT{100, 0, 300, 200}));
    CHECK(Same(Placement::VisiblePart(26, 620, 300, 200, kUltrawide), RECT{0, 0, 300, 200}));
}

void TestVisiblePartNextToANeighbour() {
    TEST("Placement: the neighbour's pixels are not the monitor's");
    // The 1080p monitor's right edge is the ultrawide's left edge. A panel closed at x = 0 is
    // entirely on the ultrawide, so none of it belongs to the 1080p monitor.
    CHECK(Same(Placement::VisiblePart(0, 700, 300, 200, kLeftMonitor), RECT{0, 0, 0, 0}));
    CHECK(Same(Placement::VisiblePart(-100, 700, 300, 200, kLeftMonitor), RECT{0, 0, 100, 200}));
}

} // namespace

void RunPlacementTests() {
    TestRightEdgeIsWhatItAlwaysWas();
    TestLeftEdgeIsTheMirror();
    TestMonitorLeftOfTheOrigin();
    TestMonitorAboveTheOrigin();
    TestPanelStaysOnTheMonitor();
    TestPanelTallerThanTheMonitor();
    TestVisiblePartOfASlidingPanel();
    TestVisiblePartNextToANeighbour();
}

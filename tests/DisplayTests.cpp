// Covers the part of the Displays tab that has no window or monitor in it: which
// monitor counts as the TV, which buttons are available / active for a given set
// of connected monitors, and how a switch is planned out of a topology. The
// planning is the part that decides whether Windows accepts the change or leaves a
// desktop with nothing at the origin, so it is driven here with hand-built
// topologies rather than left to be found on someone's hardware.
#include "TestHarness.h"

#include "DisplayTopology.h"

#include <string>
#include <vector>

using namespace DisplayTopology;

namespace {

Monitor MakeMonitor(const wchar_t* id, UINT32 targetId, bool active, UINT32 w, UINT32 h) {
    Monitor m;
    m.id = id;
    m.name = id;
    m.adapter.LowPart = 1;
    m.targetId = targetId;
    m.active = active;
    m.nativeWidth = w;
    m.nativeHeight = h;
    return m;
}

// The user's real desk: an ultrawide, a 1080p monitor and a 4K TV, all on.
std::vector<Monitor> Desk(bool lgOn, bool c27On, bool tvOn) {
    return {
        MakeMonitor(L"GSM7768", 4353, lgOn, 3440, 1440),
        MakeMonitor(L"SAM0F9D", 0, c27On, 1920, 1080),
        MakeMonitor(L"SAM7A08", 4352, tvOn, 3840, 2160),
    };
}

DISPLAYCONFIG_PATH_INFO MakePath(UINT32 targetId, UINT32 sourceId, bool active, UINT32 sourceMode,
                                 UINT32 targetMode) {
    DISPLAYCONFIG_PATH_INFO p{};
    p.sourceInfo.adapterId.LowPart = 1;
    p.sourceInfo.id = sourceId;
    p.sourceInfo.modeInfoIdx = sourceMode;
    p.targetInfo.adapterId.LowPart = 1;
    p.targetInfo.id = targetId;
    p.targetInfo.modeInfoIdx = targetMode;
    p.targetInfo.targetAvailable = TRUE;
    p.flags = active ? DISPLAYCONFIG_PATH_ACTIVE : 0;
    return p;
}

DISPLAYCONFIG_MODE_INFO MakeSourceMode(LONG x, LONG y, UINT32 w, UINT32 h) {
    DISPLAYCONFIG_MODE_INFO m{};
    m.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
    m.sourceMode.position.x = x;
    m.sourceMode.position.y = y;
    m.sourceMode.width = w;
    m.sourceMode.height = h;
    return m;
}

DISPLAYCONFIG_MODE_INFO MakeTargetMode() {
    DISPLAYCONFIG_MODE_INFO m{};
    m.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
    return m;
}

TargetKey Key(UINT32 targetId) {
    TargetKey k;
    k.adapter.LowPart = 1;
    k.targetId = targetId;
    return k;
}

// Modes laid out the way QueryDisplayConfig does it: per active path, a source
// mode then a target mode.
Config ActiveDesk() {
    Config c;
    c.modes = {
        MakeSourceMode(0, 0, 3440, 1440),     // 0: LG source
        MakeTargetMode(),                     // 1: LG target
        MakeSourceMode(-1920, 352, 1920, 1080), // 2: C27 source
        MakeTargetMode(),                     // 3
        MakeSourceMode(0, -2160, 3840, 2160), // 4: TV source
        MakeTargetMode(),                     // 5
    };
    c.paths = {
        MakePath(4353, 0, true, 0, 1),
        MakePath(0, 1, true, 2, 3),
        MakePath(4352, 2, true, 4, 5),
    };
    return c;
}

void TestTvIsTheLargestPanel() {
    TEST("Displays: with no override the TV is the largest panel, switched on or off");
    CHECK(ResolveTvId(Desk(true, true, true), L"") == L"SAM7A08");
    // The sizes come from the preferred mode, so a monitor that is off still counts.
    CHECK(ResolveTvId(Desk(false, false, true), L"") == L"SAM7A08");
    CHECK(ResolveTvId(Desk(true, true, false), L"") == L"SAM7A08");
}

void TestTvOverrideAndEdgeCases() {
    TEST("Displays: a configured TV wins; a lone or ambiguous monitor is never 'the TV'");
    CHECK(ResolveTvId(Desk(true, true, true), L"GSM7768") == L"GSM7768");
    // An override naming an unplugged monitor is still honoured - the TV buttons
    // are then unavailable rather than quietly pointing at something else.
    CHECK(ResolveTvId(Desk(true, true, true), L"NOPE000") == L"NOPE000");

    std::vector<Monitor> lone = {MakeMonitor(L"GSM7768", 1, true, 3440, 1440)};
    CHECK(ResolveTvId(lone, L"").empty());

    std::vector<Monitor> twins = {MakeMonitor(L"AAA0001", 1, true, 1920, 1080),
                                  MakeMonitor(L"BBB0002", 2, true, 1920, 1080)};
    CHECK(ResolveTvId(twins, L"").empty());
}

void TestProfileSets() {
    TEST("Displays: each profile names the right monitors");
    const auto monitors = Desk(true, true, true);
    const std::wstring tv = L"SAM7A08";

    ProfileState main = Evaluate(Profile::MainMonitors, monitors, tv);
    CHECK_EQ(main.ids.size(), size_t{2});
    CHECK(main.ids.size() == 2 && main.ids[0] == L"GSM7768" && main.ids[1] == L"SAM0F9D");

    ProfileState only = Evaluate(Profile::TvOnly, monitors, tv);
    CHECK_EQ(only.ids.size(), size_t{1});
    CHECK(only.ids.size() == 1 && only.ids[0] == L"SAM7A08");

    ProfileState all = Evaluate(Profile::AllMonitors, monitors, tv);
    CHECK_EQ(all.ids.size(), size_t{3});
}

void TestProfileActiveMarker() {
    TEST("Displays: exactly the profile that matches what is on is marked active");
    const std::wstring tv = L"SAM7A08";

    auto allOn = Desk(true, true, true);
    CHECK(Evaluate(Profile::AllMonitors, allOn, tv).active);
    CHECK(!Evaluate(Profile::MainMonitors, allOn, tv).active);
    CHECK(!Evaluate(Profile::TvOnly, allOn, tv).active);

    auto mainOn = Desk(true, true, false);
    CHECK(Evaluate(Profile::MainMonitors, mainOn, tv).active);
    CHECK(!Evaluate(Profile::AllMonitors, mainOn, tv).active);

    auto tvOn = Desk(false, false, true);
    CHECK(Evaluate(Profile::TvOnly, tvOn, tv).active);
    CHECK(!Evaluate(Profile::MainMonitors, tvOn, tv).active);

    // A layout none of the buttons produces (just the ultrawide) marks nothing.
    auto odd = Desk(true, false, false);
    CHECK(!Evaluate(Profile::MainMonitors, odd, tv).active);
    CHECK(!Evaluate(Profile::TvOnly, odd, tv).active);
    CHECK(!Evaluate(Profile::AllMonitors, odd, tv).active);
}

void TestProfilesWithoutATv() {
    TEST("Displays: no TV connected makes the TV buttons unavailable, not wrong");
    std::vector<Monitor> noTv = {MakeMonitor(L"GSM7768", 4353, true, 3440, 1440),
                                 MakeMonitor(L"SAM0F9D", 0, true, 1920, 1080)};

    ProfileState main = Evaluate(Profile::MainMonitors, noTv, L"SAM7A08");
    CHECK(main.available);
    CHECK_EQ(main.ids.size(), size_t{2});
    CHECK(!Evaluate(Profile::TvOnly, noTv, L"SAM7A08").available);
    // "All" would be identical to "main" here, so it is not offered as a separate button.
    CHECK(!Evaluate(Profile::AllMonitors, noTv, L"SAM7A08").available);

    CHECK(!Evaluate(Profile::MainMonitors, {}, L"").available);
}

void TestRemovalKeepsSurvivorsInPlace() {
    TEST("Displays: turning the TV off leaves the other two exactly where they were");
    Config plan = PlanRemoval(ActiveDesk(), {Key(4353), Key(0)});

    CHECK_EQ(plan.paths.size(), size_t{2});
    CHECK_EQ(plan.modes.size(), size_t{4});
    if (plan.paths.size() != 2 || plan.modes.size() != 4) return;

    const auto& lg = plan.modes[plan.paths[0].sourceInfo.modeInfoIdx].sourceMode;
    const auto& c27 = plan.modes[plan.paths[1].sourceInfo.modeInfoIdx].sourceMode;
    CHECK_EQ(static_cast<int>(lg.position.x), 0);
    CHECK_EQ(static_cast<int>(lg.position.y), 0);
    CHECK_EQ(static_cast<int>(c27.position.x), -1920);
    CHECK_EQ(static_cast<int>(c27.position.y), 352);

    // Every index must still point at a real mode of the right kind.
    for (const auto& p : plan.paths) {
        CHECK(p.sourceInfo.modeInfoIdx < plan.modes.size());
        CHECK(p.targetInfo.modeInfoIdx < plan.modes.size());
        CHECK(plan.modes[p.sourceInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE);
        CHECK(plan.modes[p.targetInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET);
    }
}

void TestRemovalMovesTheOriginToASurvivor() {
    TEST("Displays: TV alone becomes the origin - Windows rejects a desktop without one");
    Config plan = PlanRemoval(ActiveDesk(), {Key(4352)});

    CHECK_EQ(plan.paths.size(), size_t{1});
    CHECK_EQ(plan.modes.size(), size_t{2});
    if (plan.paths.size() != 1 || plan.modes.size() != 2) return;

    const auto& tv = plan.modes[plan.paths[0].sourceInfo.modeInfoIdx].sourceMode;
    CHECK_EQ(static_cast<int>(tv.position.x), 0);
    CHECK_EQ(static_cast<int>(tv.position.y), 0);
    CHECK_EQ(static_cast<int>(tv.width), 3840);
}

void TestRemovalShiftsSurvivorsTogether() {
    TEST("Displays: when the origin monitor goes, the rest keep their arrangement");
    // Drop the ultrawide (the origin); the 1080p and the TV stay. The first
    // survivor takes the origin and the other moves by the same amount.
    Config plan = PlanRemoval(ActiveDesk(), {Key(0), Key(4352)});

    CHECK_EQ(plan.paths.size(), size_t{2});
    if (plan.paths.size() != 2) return;
    const auto& c27 = plan.modes[plan.paths[0].sourceInfo.modeInfoIdx].sourceMode;
    const auto& tv = plan.modes[plan.paths[1].sourceInfo.modeInfoIdx].sourceMode;
    CHECK_EQ(static_cast<int>(c27.position.x), 0);
    CHECK_EQ(static_cast<int>(c27.position.y), 0);
    // Originally the TV sat 1920 to the right of and 2512 above the 1080p.
    CHECK_EQ(static_cast<int>(tv.position.x), 1920);
    CHECK_EQ(static_cast<int>(tv.position.y), -2512);
}

void TestRemovalOfEverythingIsEmpty() {
    TEST("Displays: asking to keep nothing yields an empty plan, never a blank desktop");
    Config plan = PlanRemoval(ActiveDesk(), {});
    CHECK(plan.paths.empty());
}

// Every path an adapter offers: each target can drive source 0..2.
std::vector<DISPLAYCONFIG_PATH_INFO> AllPaths(bool lgOn, bool c27On, bool tvOn) {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    const struct { UINT32 target; bool on; UINT32 onSource; } desk[] = {
        {4353, lgOn, 0}, {0, c27On, 1}, {4352, tvOn, 2}};
    for (const auto& d : desk) {
        for (UINT32 source = 0; source < 3; ++source) {
            paths.push_back(MakePath(d.target, source, d.on && source == d.onSource,
                                     DISPLAYCONFIG_PATH_MODE_IDX_INVALID,
                                     DISPLAYCONFIG_PATH_MODE_IDX_INVALID));
        }
    }
    return paths;
}

void TestTopologyKeepsActiveSourcesAndFillsTheRest() {
    TEST("Displays: switching the TV on keeps the others' sources and gives it a free one");
    auto all = AllPaths(true, true, false);
    std::vector<DISPLAYCONFIG_PATH_INFO> out;
    CHECK(PlanTopology(all, {Key(4353), Key(0), Key(4352)}, out));
    CHECK_EQ(out.size(), size_t{3});
    if (out.size() != 3) return;

    bool sourceUsed[3] = {false, false, false};
    for (const auto& p : out) {
        CHECK((p.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0);
        CHECK(p.sourceInfo.modeInfoIdx == DISPLAYCONFIG_PATH_MODE_IDX_INVALID);
        CHECK(p.sourceInfo.id < 3);
        if (p.sourceInfo.id < 3) {
            CHECK(!sourceUsed[p.sourceInfo.id]); // no two monitors share a source
            sourceUsed[p.sourceInfo.id] = true;
        }
    }
    // The two monitors that were already on kept the sources they had.
    for (const auto& p : out) {
        if (p.targetInfo.id == 4353) CHECK_EQ(static_cast<int>(p.sourceInfo.id), 0);
        if (p.targetInfo.id == 0) CHECK_EQ(static_cast<int>(p.sourceInfo.id), 1);
    }
}

void TestTopologyDropsUnwantedAndHandlesTvAlone() {
    TEST("Displays: a topology contains only the wanted monitors");
    auto all = AllPaths(true, true, true);
    std::vector<DISPLAYCONFIG_PATH_INFO> out;
    CHECK(PlanTopology(all, {Key(4352)}, out));
    CHECK_EQ(out.size(), size_t{1});
    if (out.size() == 1) CHECK_EQ(static_cast<int>(out[0].targetInfo.id), 4352);
}

void TestTopologyFailsForAMissingTarget() {
    TEST("Displays: asking for a monitor that has no path fails instead of guessing");
    auto all = AllPaths(true, true, false);
    std::vector<DISPLAYCONFIG_PATH_INFO> out;
    CHECK(!PlanTopology(all, {Key(9999)}, out));
}

void TestGdiNameOfIsTheBridgeToAHandle() {
    TEST("Displays: an EDID id maps to the GDI device of that monitor, if it is on");
    std::vector<Monitor> desk = Desk(true, true, false);
    desk[0].gdiName = L"\\\\.\\DISPLAY2"; // the ultrawide
    desk[1].gdiName = L"\\\\.\\DISPLAY1"; // the 1080p monitor
    CHECK(GdiNameOf(desk, L"GSM7768") == L"\\\\.\\DISPLAY2");
    CHECK(GdiNameOf(desk, L"SAM0F9D") == L"\\\\.\\DISPLAY1");

    // The TV is plugged in but switched off, so it has no device to draw through: a tab
    // configured for it cannot dock there, and the caller falls back to the primary monitor.
    CHECK(GdiNameOf(desk, L"SAM7A08").empty());
    // Not plugged in at all, and no id at all, are the same answer.
    CHECK(GdiNameOf(desk, L"XXX0000").empty());
    CHECK(GdiNameOf(desk, L"").empty());
}

void TestGdiNameOfIgnoresAStaleName() {
    TEST("Displays: a switched-off monitor is not found even if a name was left on it");
    std::vector<Monitor> desk = Desk(true, false, false);
    desk[1].gdiName = L"\\\\.\\DISPLAY1";
    CHECK(GdiNameOf(desk, L"SAM0F9D").empty());
}

void TestFindHandleAgainstTheRealDesktop() {
    TEST("Displays: every active monitor's id finds a real handle, an unknown one finds none");
    // Runs against whatever desktop the test runs on. On a headless runner there is nothing to
    // enumerate, and the loop is empty: what is left to check then is only the "no" answers.
    for (const Monitor& m : Enumerate()) {
        if (!m.active) {
            CHECK(FindHandle(m.id) == nullptr);
            continue;
        }
        HMONITOR handle = FindHandle(m.id);
        CHECK(handle != nullptr);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        CHECK(handle && GetMonitorInfoW(handle, &info));
        if (handle) CHECK(_wcsicmp(info.szDevice, m.gdiName.c_str()) == 0);
    }
    CHECK(FindHandle(L"NOSUCH0") == nullptr);
    CHECK(FindHandle(L"") == nullptr);
}

} // namespace

void RunDisplayTests() {
    TestGdiNameOfIsTheBridgeToAHandle();
    TestGdiNameOfIgnoresAStaleName();
    TestFindHandleAgainstTheRealDesktop();
    TestTvIsTheLargestPanel();
    TestTvOverrideAndEdgeCases();
    TestProfileSets();
    TestProfileActiveMarker();
    TestProfilesWithoutATv();
    TestRemovalKeepsSurvivorsInPlace();
    TestRemovalMovesTheOriginToASurvivor();
    TestRemovalShiftsSurvivorsTogether();
    TestRemovalOfEverythingIsEmpty();
    TestTopologyKeepsActiveSourcesAndFillsTheRest();
    TestTopologyDropsUnwantedAndHandlesTvAlone();
    TestTopologyFailsForAMissingTarget();
}

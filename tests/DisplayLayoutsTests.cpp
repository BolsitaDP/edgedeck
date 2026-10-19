// The Displays tab's layouts: the automatic ones, carrying the four old fixed ones over, which
// layout is on, the cleaning of a saved list, and the config.txt round trip. Switching monitors
// for real is tools/scripts/Test-DisplaySwitching.ps1.
#include "TestHarness.h"

#include "Config.h"
#include "DisplayLayouts.h"
#include "Strings.h"

#include <algorithm>
#include <string>
#include <vector>

using DisplayLayouts::Layout;
using DisplayTopology::Monitor;

namespace {

Monitor Make(const wchar_t* id, const wchar_t* name, bool active, bool primary, UINT32 w, UINT32 h) {
    Monitor m;
    m.id = id;
    m.name = name;
    m.adapter.LowPart = 1;
    m.active = active;
    m.primary = primary;
    m.nativeWidth = w;
    m.nativeHeight = h;
    return m;
}

// An ultrawide (primary), a 1080p monitor and a 4K TV.
std::vector<Monitor> Desk(bool lg, bool c27, bool tv) {
    return {Make(L"GSM7768", L"LG ULTRAWIDE", lg, lg, 3440, 1440),
            Make(L"SAM0F9D", L"C27R50x", c27, false, 1920, 1080),
            Make(L"SAM7A08", L"SAMSUNG", tv, !lg && tv, 3840, 2160)};
}

std::vector<std::wstring> Sorted(std::vector<std::wstring> v) {
    std::sort(v.begin(), v.end());
    return v;
}

void TestAutomaticLayouts() {
    TEST("DisplayLayouts: with nothing saved, all monitors and each one alone; one monitor has none");
    Strings::SetCurrent(Language::English);
    const auto layouts = DisplayLayouts::Automatic(Desk(true, true, true));
    CHECK_EQ(layouts.size(), size_t{4});
    if (layouts.size() == 4) {
        CHECK(layouts[0].name == L"All monitors");
        CHECK_EQ(layouts[0].ids.size(), size_t{3});
        CHECK(layouts[1].name == L"Only LG ULTRAWIDE");
        CHECK(layouts[1].ids == std::vector<std::wstring>{L"GSM7768"});
        CHECK(layouts[3].name == L"Only SAMSUNG");
    }
    CHECK(DisplayLayouts::Automatic({Make(L"GSM7768", L"LG", true, true, 3440, 1440)}).empty());
    CHECK(DisplayLayouts::Automatic({}).empty());
}

void TestTheOldFourAreCarriedOver() {
    TEST("DisplayLayouts: an existing setup's four fixed buttons become saved layouts, same names and order");
    Strings::SetCurrent(Language::English);
    const auto monitors = Desk(true, true, false);
    const std::wstring tv = DisplayTopology::ResolveTvId(monitors, L"");
    const std::wstring main = DisplayTopology::ResolveMainId(monitors, tv, L"");
    CHECK(tv == L"SAM7A08");
    const auto layouts = DisplayLayouts::FromProfiles(monitors, tv, main);
    CHECK_EQ(layouts.size(), size_t{4});
    if (layouts.size() == 4) {
        CHECK(layouts[0].name == L"Monitors 1 + 2");
        CHECK(Sorted(layouts[0].ids) == Sorted({L"GSM7768", L"SAM0F9D"}));
        CHECK(layouts[1].name == L"TV only" && layouts[1].ids == std::vector<std::wstring>{L"SAM7A08"});
        CHECK(layouts[2].name == L"All monitors" && layouts[2].ids.size() == 3);
        CHECK(layouts[3].name == L"Main monitor only" && layouts[3].ids == std::vector<std::wstring>{L"GSM7768"});
    }

    // With two monitors, "main only" would be the same set as another button: it is not repeated.
    std::vector<Monitor> two = {Make(L"GSM7768", L"LG", true, true, 3440, 1440),
                                Make(L"SAM7A08", L"TV", false, false, 3840, 2160)};
    const std::wstring tv2 = DisplayTopology::ResolveTvId(two, L"");
    const auto fewer = DisplayLayouts::FromProfiles(two, tv2, DisplayTopology::ResolveMainId(two, tv2, L""));
    for (size_t i = 0; i < fewer.size(); ++i) {
        for (size_t j = i + 1; j < fewer.size(); ++j) CHECK(Sorted(fewer[i].ids) != Sorted(fewer[j].ids));
    }
}

void TestWhichLayoutIsOn() {
    TEST("DisplayLayouts: a layout is on when exactly its monitors are, and unavailable when one is unplugged");
    const Layout desk{L"Desk", {L"GSM7768", L"SAM0F9D"}};
    auto state = DisplayLayouts::Evaluate(desk, Desk(true, true, false));
    CHECK(state.available && state.active);
    state = DisplayLayouts::Evaluate(desk, Desk(true, true, true)); // the TV too: not exactly
    CHECK(state.available && !state.active);
    state = DisplayLayouts::Evaluate(desk, Desk(false, false, true)); // off but connected: available
    CHECK(state.available && !state.active);

    const Layout gone{L"Old", {L"GSM7768", L"DEL4321"}};
    state = DisplayLayouts::Evaluate(gone, Desk(true, true, true));
    CHECK(!state.available && !state.active);
    CHECK(state.missing == std::vector<std::wstring>{L"DEL4321"});
}

void TestSanitize() {
    TEST("DisplayLayouts: a saved list is cleaned - names, ids, empty layouts, and at most nine");
    std::vector<Layout> in = {
        {L"  Desk  ", {L"gsm7768", L" SAM0F9D ", L"GSM7768"}},
        {L"", {L"GSM7768"}},
        {L"No monitors", {}},
        {std::wstring(80, L'n'), {L"X"}},
    };
    const auto out = DisplayLayouts::Sanitize(in);
    CHECK_EQ(out.size(), size_t{2});
    if (out.size() == 2) {
        CHECK(out[0].name == L"Desk");
        CHECK(out[0].ids == (std::vector<std::wstring>{L"GSM7768", L"SAM0F9D"}));
        CHECK_EQ(out[1].name.size(), DisplayLayouts::kMaxNameLength);
    }
    std::vector<Layout> many;
    for (int i = 0; i < 20; ++i) many.push_back({L"L" + std::to_wstring(i), {L"A"}});
    CHECK_EQ(DisplayLayouts::Sanitize(many).size(), DisplayLayouts::kMaxLayouts);

    CHECK(DisplayLayouts::SplitIds(L" gsm7768 , ,SAM0F9D") == (std::vector<std::wstring>{L"GSM7768", L"SAM0F9D"}));
    CHECK(DisplayLayouts::JoinIds({L"A", L"B"}) == L"A,B");
    CHECK(DisplayLayouts::NameFor({L"GSM7768", L"SAM0F9D"}, Desk(true, true, true)) == L"LG ULTRAWIDE + C27R50x");
    CHECK(DisplayLayouts::NameFor({L"DEL4321"}, Desk(true, true, true)) == L"DEL4321");
}

void TestKeptInTheSettings() {
    TEST("DisplayLayouts: the layouts and the carried-over mark are saved and read back");
    std::vector<TabSettings> tabs = {{WidgetType::Displays, 0.5f, 26.0f, 76.0f, 300.0f}};
    Config::SetDisplayLayoutList({{L"Desk", {L"GSM7768", L"SAM0F9D"}}, {L"Sofa", {L"SAM7A08"}}});
    Config::SetDisplayLayoutsCarried(true);
    CHECK(Config::Save(tabs));
    Config::SetDisplayLayoutList({});
    Config::SetDisplayLayoutsCarried(false);
    Config::LoadOrDefault();
    const auto back = Config::DisplayLayoutList();
    CHECK_EQ(back.size(), size_t{2});
    if (back.size() == 2) {
        CHECK(back[0].name == L"Desk" && back[0].ids.size() == 2);
        CHECK(back[1].name == L"Sofa" && back[1].ids == std::vector<std::wstring>{L"SAM7A08"});
    }
    CHECK(Config::DisplayLayoutsCarried());

    // Nothing saved reads back as nothing: the automatic layouts.
    Config::SetDisplayLayoutList({});
    Config::SetDisplayLayoutsCarried(false);
    CHECK(Config::Save(tabs));
    Config::SetDisplayLayoutList({{L"Stale", {L"A"}}});
    Config::LoadOrDefault();
    CHECK(Config::DisplayLayoutList().empty());
    CHECK(!Config::DisplayLayoutsCarried());
}

} // namespace

void RunDisplayLayoutsTests() {
    TestAutomaticLayouts();
    TestTheOldFourAreCarriedOver();
    TestWhichLayoutIsOn();
    TestSanitize();
    TestKeptInTheSettings();
}

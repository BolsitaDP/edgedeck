// A laptop's built-in screen, set through WMI: telling a built-in connection from a cable, finding
// the WMI panel that is a given monitor, and asking WMI on a machine that may have no such panel.
// Setting a real backlight needs a laptop; everything that decides which panel is set is here.
#include "TestHarness.h"

#include "LaptopBrightness.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using LaptopBrightness::Panel;

namespace {

void TestTheIdInAWmiName() {
    TEST("LaptopBrightness: the EDID id is read out of a WMI instance name");
    using LaptopBrightness::EdidIdFromInstanceName;
    CHECK(EdidIdFromInstanceName(L"DISPLAY\\BOE0812\\4&2a8b3b5&0&UID265988_0") == L"BOE0812");
    CHECK(EdidIdFromInstanceName(L"DISPLAY\\sdc4161\\5&1f0e&0&UID8388688_0") == L"SDC4161"); // any case
    CHECK(EdidIdFromInstanceName(L"DISPLAY\\LGD06FF") == L"LGD06FF"); // nothing after it
    CHECK(EdidIdFromInstanceName(L"nonsense").empty());
    CHECK(EdidIdFromInstanceName(L"").empty());
}

void TestBuiltInConnections() {
    TEST("LaptopBrightness: a built-in screen's connection is told from a cable's");
    CHECK(LaptopBrightness::IsBuiltIn(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL));
    CHECK(LaptopBrightness::IsBuiltIn(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED));
    CHECK(LaptopBrightness::IsBuiltIn(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED));
    CHECK(LaptopBrightness::IsBuiltIn(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS));
    CHECK(!LaptopBrightness::IsBuiltIn(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI));
    CHECK(!LaptopBrightness::IsBuiltIn(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL));
    CHECK(!LaptopBrightness::IsBuiltIn(DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI));
}

void TestWhichPanelIsWhichMonitor() {
    TEST("LaptopBrightness: a monitor gets the WMI panel with its id, or the only one, and never a guess");
    using LaptopBrightness::MatchPanel;
    const std::vector<Panel> two = {{L"DISPLAY\\BOE0812\\a", L"BOE0812", 40}, {L"DISPLAY\\SDC4161\\b", L"SDC4161", 70}};
    CHECK_EQ(MatchPanel(two, L"SDC4161", 2), 1);
    CHECK_EQ(MatchPanel(two, L"boe0812", 2), 0);
    CHECK_EQ(MatchPanel(two, L"XXX0000", 2), -1); // two panels, no id that fits: not a guess
    CHECK_EQ(MatchPanel(two, L"", 2), -1);

    const std::vector<Panel> one = {{L"DISPLAY\\BOE0812\\a", L"BOE0812", 40}};
    CHECK_EQ(MatchPanel(one, L"", 1), 0);        // the only panel and the only built-in screen
    CHECK_EQ(MatchPanel(one, L"OTHER01", 1), 0); // ids that disagree (a driver's own), still the only pair
    CHECK_EQ(MatchPanel(one, L"", 2), -1);       // two built-in screens, one panel: which is it?
    CHECK_EQ(MatchPanel({}, L"BOE0812", 1), -1);
}

void TestAskingWmiIsSafe() {
    TEST("LaptopBrightness: asking WMI answers - panels with a level, or none - and does not hang");
    const auto start = std::chrono::steady_clock::now();
    const std::vector<Panel> panels = LaptopBrightness::Read();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("    WMI: %zu panel(s) in %lld ms\n", panels.size(), static_cast<long long>(ms));
    for (const Panel& p : panels) {
        CHECK(!p.instanceName.empty());
        CHECK(p.percent >= 0 && p.percent <= 100);
    }
    CHECK(ms < 15000);
    // A panel that does not exist cannot be set.
    CHECK(!LaptopBrightness::Set(L"DISPLAY\\NOPE000\\0&0&0&UID0_0", 50));
}

} // namespace

void RunLaptopBrightnessTests() {
    TestTheIdInAWmiName();
    TestBuiltInConnections();
    TestWhichPanelIsWhichMonitor();
    TestAskingWmiIsSafe();
}

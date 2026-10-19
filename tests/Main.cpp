// Single entry point for the test binary. Each suite file exposes one
// Run*Tests() function; this just calls them in order and reports.
#include "TestHarness.h"

#include <filesystem>
#include <string>

void RunLyricsTests();
void RunConfigTests();
void RunDisplayTests();
void RunLayeredTests();
void RunNoticeTests();
void RunAutostartTests();
void RunFullscreenTests();
void RunPlacementTests();
void RunIconTests();
void RunQuickActionTests();
void RunActionIconTests();
void RunRecentFilesTests();
void RunStringsTests();
void RunCrashTests();

int wmain() {
    // The suites read and write the config file and the log under %LOCALAPPDATA%\EdgeDeck. They
    // used to do that in the real folder, saving the user's file first and putting it back after:
    // correct, until a run was killed half way and left a test's settings in a real install, and
    // meanwhile every run rewrote the real file's timestamp. Pointed at a scratch folder instead,
    // a test run cannot reach anything that belongs to a running EdgeDeck.
    std::error_code ec;
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path(ec) /
        (L"edgedeck-tests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(scratch, ec);
    if (!ec) SetEnvironmentVariableW(L"LOCALAPPDATA", scratch.c_str());

    std::printf("EdgeDeck unit tests\n\n");
    RunLyricsTests();
    std::printf("\n");
    RunConfigTests();
    std::printf("\n");
    RunDisplayTests();
    std::printf("\n");
    RunLayeredTests();
    std::printf("\n");
    RunNoticeTests();
    std::printf("\n");
    RunAutostartTests();
    std::printf("\n");
    RunFullscreenTests();
    std::printf("\n");
    RunPlacementTests();
    std::printf("\n");
    RunIconTests();
    std::printf("\n");
    RunQuickActionTests();
    std::printf("\n");
    RunActionIconTests();
    std::printf("\n");
    RunRecentFilesTests();
    std::printf("\n");
    RunStringsTests();
    std::printf("\n");
    RunCrashTests();

    const int failures = testing::Summary();
    std::filesystem::remove_all(scratch, ec);
    return failures;
}

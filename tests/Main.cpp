// Single entry point for the test binary. Each suite file exposes one
// Run*Tests() function; this just calls them in order and reports.
#include "TestHarness.h"

void RunLyricsTests();
void RunConfigTests();
void RunDisplayTests();
void RunLayeredTests();
void RunNoticeTests();
void RunAutostartTests();

int wmain() {
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
    return testing::Summary();
}

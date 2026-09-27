// Single entry point for the test binary. Each suite file exposes one
// Run*Tests() function; this just calls them in order and reports.
#include "TestHarness.h"

void RunLyricsTests();
void RunConfigTests();

int wmain() {
    std::printf("EdgeDeck unit tests\n\n");
    RunLyricsTests();
    std::printf("\n");
    RunConfigTests();
    return testing::Summary();
}

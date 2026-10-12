// The Quick Actions list's rules and what running a row involves. The parts that need a desktop
// (a program actually starting) are covered by tools/scripts/Test-QuickActions.ps1; what is here
// is everything that can go wrong without one: what a valid list is, how a target is expanded, and
// what the user is told when Windows says no.
#include "TestHarness.h"

#include "Actions.h"
#include "QuickAction.h"

#include <string>
#include <vector>

namespace {

void TestDefaultsAreTheOriginalThree() {
    TEST("QuickActions: the defaults are what the panel always showed");
    const auto& d = QuickActions::Defaults();
    CHECK_EQ(d.size(), size_t{3});
    if (d.size() == 3) {
        CHECK(d[0].name == L"Open Notepad" && d[0].target == L"notepad.exe");
        CHECK(d[1].name == L"Open Calculator" && d[1].target == L"calc.exe");
        CHECK(d[2].name == L"Show Desktop" && d[2].target == QuickActions::kShowDesktop);
    }
    // And they are themselves a valid list: sanitising changes nothing.
    CHECK_EQ(QuickActions::Sanitize(d).size(), d.size());
}

void TestSanitizeCleansEachField() {
    TEST("QuickActions: Sanitize trims, removes line breaks, and caps lengths");
    std::vector<QuickAction> in = {
        {L"  Padded  ", L"\tprog.exe  ", L" -x "},
        {L"Two\r\nLines", L"a\nb", L""},
        {std::wstring(100, L'n'), std::wstring(900, L't'), std::wstring(400, L'a')},
    };
    const auto out = QuickActions::Sanitize(in);
    CHECK_EQ(out.size(), size_t{3});
    if (out.size() == 3) {
        CHECK(out[0].name == L"Padded" && out[0].target == L"prog.exe" && out[0].arguments == L"-x");
        // The file holds one value per line, so a break would end the value and start another.
        CHECK(out[1].name == L"TwoLines" && out[1].target == L"ab");
        CHECK_EQ(out[2].name.size(), QuickActions::kMaxNameLength);
        CHECK_EQ(out[2].target.size(), QuickActions::kMaxTargetLength);
        CHECK_EQ(out[2].arguments.size(), QuickActions::kMaxArgumentsLength);
    }
}

void TestSanitizeDropsWhatCannotRun() {
    TEST("QuickActions: a row needs a name and something to open; the count is capped");
    std::vector<QuickAction> in = {
        {L"", L"x.exe", L""},          // no name
        {L"No target", L"   ", L""},   // nothing to open
        {L"Good", L"y.exe", L""},
    };
    CHECK_EQ(QuickActions::Sanitize(in).size(), size_t{1});

    std::vector<QuickAction> many;
    for (int i = 0; i < 40; ++i) many.push_back({L"n" + std::to_wstring(i), L"t", L""});
    const auto capped = QuickActions::Sanitize(many);
    CHECK_EQ(capped.size(), QuickActions::kMaxActions);
    CHECK(capped.front().name == L"n0"); // the first ones, in order
}

void TestVariablesAreExpanded() {
    TEST("Actions: %Variables% in a target are expanded, and unknown ones are left as written");
    SetEnvironmentVariableW(L"EDGEDECK_TEST_DIR", L"C:\\Tools");
    CHECK(Actions::ExpandVariables(L"%EDGEDECK_TEST_DIR%\\x.exe") == L"C:\\Tools\\x.exe");
    CHECK(Actions::ExpandVariables(L"notepad.exe") == L"notepad.exe");
    CHECK(Actions::ExpandVariables(L"https://example.org/100%") == L"https://example.org/100%");
    // Left as typed, which is what makes the failure message readable.
    CHECK(Actions::ExpandVariables(L"%NO_SUCH_VARIABLE_EDGEDECK%\\x") == L"%NO_SUCH_VARIABLE_EDGEDECK%\\x");
    SetEnvironmentVariableW(L"EDGEDECK_TEST_DIR", nullptr);
}

void TestShellErrorsAreDescribed() {
    TEST("Actions: the reasons ShellExecute fails are put in words, and unknown ones keep their number");
    CHECK(Actions::DescribeShellError(ERROR_FILE_NOT_FOUND) == L"not found");
    CHECK(Actions::DescribeShellError(ERROR_PATH_NOT_FOUND) == L"not found");
    CHECK(Actions::DescribeShellError(ERROR_ACCESS_DENIED) == L"access denied");
    CHECK(Actions::DescribeShellError(ERROR_CANCELLED) == L"cancelled");
    CHECK(Actions::DescribeShellError(ERROR_NO_ASSOCIATION).find(L"kind of file") != std::wstring::npos);
    CHECK(Actions::DescribeShellError(12345) == L"Windows error 12345");
}

void TestARowThatCannotRunSaysWhy() {
    TEST("Actions: a row whose target does not exist fails with a sentence naming it, and no dialog");
    std::wstring problem;
    const QuickAction missing{L"Gone", L"C:\\no\\such\\folder\\edgedeck-test-missing.exe", L""};
    CHECK(!Actions::Run(missing, problem));
    CHECK(problem.find(L"\"Gone\"") != std::wstring::npos);
    CHECK(problem.find(L"not found") != std::wstring::npos);
}

} // namespace

void RunQuickActionTests() {
    TestDefaultsAreTheOriginalThree();
    TestSanitizeCleansEachField();
    TestSanitizeDropsWhatCannotRun();
    TestVariablesAreExpanded();
    TestShellErrorsAreDescribed();
    TestARowThatCannotRunSaysWhy();
}
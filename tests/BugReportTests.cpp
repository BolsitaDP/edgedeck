// "Report a problem...": the end of the log is cut cleanly, the user's folder is masked, and the
// new-issue address fills in the bug template's fields. Opening the browser and the clipboard are
// the app's; everything that decides what they get is here.
#include "TestHarness.h"

#include "BugReport.h"

#include <string>
#include <vector>

namespace {

void TestLastLines() {
    TEST("BugReport: the end of the log is whole lines, and a line cut in half by the read is dropped");
    CHECK(BugReport::LastLines(L"a\nb\nc\n", 2, false) == L"b\nc\n");
    CHECK(BugReport::LastLines(L"a\r\nb\r\nc\r\n", 2, false) == L"b\r\nc\n");
    CHECK(BugReport::LastLines(L"a\nb\nc", 10, false) == L"a\nb\nc\n"); // fewer than asked: all of them
    CHECK(BugReport::LastLines(L"partial\nb\nc\n", 10, true) == L"b\nc\n");
    CHECK(BugReport::LastLines(L"partial", 10, true).empty());
    CHECK(BugReport::LastLines(L"", 10, false).empty());
    CHECK(BugReport::LastLines(L"\n\n", 10, false).empty());
    CHECK(BugReport::LastLines(L"\nfirst\n", 10, false) == L"first\n");
}

void TestMask() {
    TEST("BugReport: the user's folder is masked wherever it appears, in any case");
    const std::wstring profile = L"C:\\Users\\Ana";
    CHECK(BugReport::Mask(L"open C:\\Users\\Ana\\Music\\x.mp3 and c:\\users\\ana\\y", profile, L"%USERPROFILE%") ==
          L"open %USERPROFILE%\\Music\\x.mp3 and %USERPROFILE%\\y");
    CHECK(BugReport::Mask(L"nothing here", profile, L"%USERPROFILE%") == L"nothing here");
    CHECK(BugReport::Mask(L"C:\\Users\\Ana", L"", L"%USERPROFILE%") == L"C:\\Users\\Ana"); // no profile known
    CHECK(BugReport::Mask(L"aaaa", L"aa", L"a") == L"aa"); // a replacement is not searched again
}

void TestClipboardText() {
    TEST("BugReport: the clipboard has the masked log and the newest crash dump to attach");
    BugReport::Facts facts;
    facts.userProfile = L"C:\\Users\\Ana";
    facts.log = L"12:00:01 error Recent: cannot list C:\\Users\\Ana\\Downloads\n";
    std::wstring text = BugReport::ClipboardText(facts);
    CHECK(text.find(L"Ana") == std::wstring::npos);
    CHECK(text.find(L"%USERPROFILE%\\Downloads") != std::wstring::npos);
    CHECK(text.find(L".dmp") == std::wstring::npos);

    facts.newestDump = L"EdgeDeck-20261019-120000-42.dmp";
    text = BugReport::ClipboardText(facts);
    CHECK(text.find(L"crashes\\EdgeDeck-20261019-120000-42.dmp") != std::wstring::npos);
}

void TestIssueUrl() {
    TEST("BugReport: the new-issue address fills the template's fields, encoded");
    BugReport::Facts facts;
    facts.version = L"1.0.0";
    facts.architecture = L"x64";
    facts.windows = L"Windows 11 24H2 (build 26100.2605)";
    facts.monitors = {{3440, 1440, 100, true}, {3840, 2160, 150, false}};
    const std::wstring url = BugReport::IssueUrl(L"https://github.com/someone/edgedeck", facts);
    CHECK(url.rfind(L"https://github.com/someone/edgedeck/issues/new?template=bug_report.yml&", 0) == 0);
    CHECK(url.find(L"&version=1.0.0%20%28x64%29") != std::wstring::npos);
    CHECK(url.find(L"&windows=Windows%2011%2024H2%20%28build%2026100.2605%29") != std::wstring::npos);
    CHECK(url.find(L"&displays=3440x1440%20at%20100%25%20%28primary%29%0A3840x2160%20at%20150%25") !=
          std::wstring::npos);
    // Nothing in it a browser would read as the end of the value, or of the address.
    CHECK(url.find(L' ') == std::wstring::npos && url.find(L'#') == std::wstring::npos);

    facts.monitors.clear();
    CHECK(BugReport::IssueUrl(L"https://x", facts).find(L"displays=") == std::wstring::npos);
    CHECK(BugReport::EncodeQueryValue(L"\u00F1&=") == L"%C3%B1%26%3D"); // UTF-8, then encoded
}

void TestGather() {
    TEST("BugReport: what is gathered from this machine names the version, Windows and the build");
    const BugReport::Facts facts = BugReport::Gather(L"9.9.9");
    CHECK(facts.version == L"9.9.9");
    CHECK(facts.windows.rfind(L"Windows 1", 0) == 0);
    CHECK(facts.architecture.rfind(L"x64", 0) == 0 || facts.architecture.rfind(L"ARM64", 0) == 0);
    for (const auto& m : facts.monitors) CHECK(m.width > 0 && m.height > 0 && m.scalePercent >= 100);
}

} // namespace

void RunBugReportTests() {
    TestLastLines();
    TestMask();
    TestClipboardText();
    TestIssueUrl();
    TestGather();
}

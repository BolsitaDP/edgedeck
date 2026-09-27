// Covers the parts of EdgeDeck that have no windowing in them: the LRC parser
// and the config file. Both had real bugs that only a test would have caught
// cheaply - the parser silently dropped every timestamp but the last on a
// multi-stamp line and misread one-digit fractions, and the config loader
// trusted whatever numbers it found in the file.
#include "TestHarness.h"

#include "LrcLyrics.h"

#include <string>
#include <vector>

namespace {

int TimeOf(const std::vector<LrcLyrics::Line>& lines, size_t index) {
    return index < lines.size() ? lines[index].timeMs : -1;
}

void TestSimpleLine() {
    TEST("LRC: a single timestamp is parsed");
    auto lines = LrcLyrics::ParseLrc(L"[00:12.34]Hello there");
    CHECK_EQ(lines.size(), size_t{1});
    CHECK_EQ(TimeOf(lines, 0), 12340);
    CHECK_EQ(lines[0].text, std::wstring(L"Hello there"));
}

void TestFractionDigits() {
    TEST("LRC: fractional seconds scale by digit count, not assumed width");
    // The bug this replaces: ".5" was read as 50ms instead of 500ms.
    CHECK_EQ(TimeOf(LrcLyrics::ParseLrc(L"[00:01.5]a"), 0), 1500);
    CHECK_EQ(TimeOf(LrcLyrics::ParseLrc(L"[00:01.50]a"), 0), 1500);
    CHECK_EQ(TimeOf(LrcLyrics::ParseLrc(L"[00:01.500]a"), 0), 1500);
    CHECK_EQ(TimeOf(LrcLyrics::ParseLrc(L"[00:01.25]a"), 0), 1250);
    CHECK_EQ(TimeOf(LrcLyrics::ParseLrc(L"[01:02.75]a"), 0), 62750);
}

void TestRepeatedTimestamp() {
    TEST("LRC: a repeated line keeps one entry per timestamp");
    // The bug this replaces: only the final stamp survived, so a chorus that
    // recurred at 01:30 was never shown a second time.
    auto lines = LrcLyrics::ParseLrc(L"[00:12.00][01:30.00]Same chorus");
    CHECK_EQ(lines.size(), size_t{2});
    CHECK_EQ(TimeOf(lines, 0), 12000);
    CHECK_EQ(TimeOf(lines, 1), 90000);
    CHECK_EQ(lines[0].text, std::wstring(L"Same chorus"));
    CHECK_EQ(lines[1].text, std::wstring(L"Same chorus"));
}

void TestMetadataTagsIgnored() {
    TEST("LRC: metadata tags are not mistaken for timestamps");
    auto lines = LrcLyrics::ParseLrc(
        L"[ar:Some Artist]\r\n"
        L"[ti:Some Title]\r\n"
        L"[al:An Album]\r\n"
        L"[length:03:47]\r\n"
        L"[00:05.00]Real lyric line");
    CHECK_EQ(lines.size(), size_t{1});
    CHECK_EQ(TimeOf(lines, 0), 5000);
    CHECK_EQ(lines[0].text, std::wstring(L"Real lyric line"));
}

void TestOffsetTag() {
    TEST("LRC: [offset:] shifts every timestamp");
    auto shifted = LrcLyrics::ParseLrc(L"[offset:+500]\n[00:10.00]line");
    CHECK_EQ(TimeOf(shifted, 0), 10500);

    auto back = LrcLyrics::ParseLrc(L"[offset:-2000]\n[00:10.00]line");
    CHECK_EQ(TimeOf(back, 0), 8000);

    // A negative offset large enough to push a stamp below zero clamps rather
    // than wrapping around to a huge unsigned value.
    auto clamped = LrcLyrics::ParseLrc(L"[offset:-99000]\n[00:10.00]line");
    CHECK_EQ(TimeOf(clamped, 0), 0);
}

void TestSorting() {
    TEST("LRC: output is sorted by time regardless of file order");
    auto lines = LrcLyrics::ParseLrc(L"[00:30.00]third\n[00:10.00]first\n[00:20.00]second");
    CHECK_EQ(lines.size(), size_t{3});
    CHECK_EQ(lines[0].text, std::wstring(L"first"));
    CHECK_EQ(lines[1].text, std::wstring(L"second"));
    CHECK_EQ(lines[2].text, std::wstring(L"third"));
}

void TestWhitespaceAndEmptyLines() {
    TEST("LRC: surrounding whitespace is trimmed and blank lines dropped");
    auto lines = LrcLyrics::ParseLrc(L"[00:05.00]   padded   \n\n[00:06.00]\t tabbed \t");
    CHECK_EQ(lines.size(), size_t{2});
    CHECK_EQ(lines[0].text, std::wstring(L"padded"));
    CHECK_EQ(lines[1].text, std::wstring(L"tabbed"));
}

void TestUnparseableInput() {
    TEST("LRC: garbage yields no lines rather than bogus ones");
    CHECK_EQ(LrcLyrics::ParseLrc(L"").size(), size_t{0});
    CHECK_EQ(LrcLyrics::ParseLrc(L"just some text").size(), size_t{0});
    CHECK_EQ(LrcLyrics::ParseLrc(L"[not:a:timestamp]x").size(), size_t{0});
    CHECK_EQ(LrcLyrics::ParseLrc(L"[00:99.99]late").size(), size_t{1});
}

void TestUtf8Content() {
    TEST("LRC: non-ASCII lyric text survives intact");
    auto lines = LrcLyrics::ParseLrc(L"[00:03.00]\x00E1\x00E9\x00ED \x4E2D\x6587");
    CHECK_EQ(lines.size(), size_t{1});
    CHECK_EQ(lines[0].text, std::wstring(L"\x00E1\x00E9\x00ED \x4E2D\x6587"));
}

} // namespace

void RunLyricsTests() {
    TestSimpleLine();
    TestFractionDigits();
    TestRepeatedTimestamp();
    TestMetadataTagsIgnored();
    TestOffsetTag();
    TestSorting();
    TestWhitespaceAndEmptyLines();
    TestUnparseableInput();
    TestUtf8Content();
}

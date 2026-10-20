// Covers the parts of EdgeDeck that have no windowing in them: the LRC parser
// and the config file. Both had real bugs that only a test would have caught
// cheaply - the parser silently dropped every timestamp but the last on a
// multi-stamp line and misread one-digit fractions, and the config loader
// trusted whatever numbers it found in the file.
#include "TestHarness.h"

#include "LrcLyrics.h"
#include "MediaPosition.h"

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

using LrcLyrics::Candidate;

void TestPickPrefersSyncedOfTheSameLength() {
    TEST("Lyrics: synced lyrics of the playing length are chosen, over plain and over other lengths");
    std::vector<Candidate> list = {
        {371000, false, L"", L"plain words"},           // plain only (what LRCLIB had for the song that failed)
        {280000, false, L"[00:01.00]other cut", L""},   // synced, but another recording
        {366000, false, L"[00:01.00]this one", L""},    // synced, 0.5 s from the player's length
    };
    auto c = LrcLyrics::PickLyrics(list, 366560);
    CHECK(c.index == 2 && c.synced);
    // Unknown length: any synced record will do; the first one found is as good as another.
    c = LrcLyrics::PickLyrics(list, 0);
    CHECK(c.synced && (c.index == 1 || c.index == 2));
}

void TestPickFallsBackToPlain() {
    TEST("Lyrics: with no synced lyrics of that length, the plain ones closest in length");
    std::vector<Candidate> plainOnly = {
        {280000, false, L"", L"short version"},
        {371000, false, L"", L"long version"},
    };
    auto c = LrcLyrics::PickLyrics(plainOnly, 366560);
    CHECK(c.index == 1 && !c.synced);

    // Synced, but for a recording 90 s longer: plain lyrics are better than a wrong highlight.
    std::vector<Candidate> mixed = {{456000, false, L"[00:01.00]x", L""}, {360000, false, L"", L"words"}};
    c = LrcLyrics::PickLyrics(mixed, 366560);
    CHECK(c.index == 1 && !c.synced);
    // ...and when there is nothing else, those synced lyrics after all.
    c = LrcLyrics::PickLyrics({{456000, false, L"[00:01.00]x", L""}}, 366560);
    CHECK(c.index == 0 && c.synced);
}

void TestPickSkipsInstrumentalAndEmpty() {
    TEST("Lyrics: instrumental and empty records are never chosen");
    std::vector<Candidate> list = {{366000, true, L"[00:01.00]la", L"la"}, {366000, false, L"", L""}};
    CHECK(LrcLyrics::PickLyrics(list, 366000).index == -1);
    CHECK(LrcLyrics::PickLyrics({}, 366000).index == -1);
}

void TestPlainLines() {
    TEST("Lyrics: plain lyrics become lines without times, verses kept apart by one blank line");
    auto lines = LrcLyrics::PlainLines(L"\r\nUno\r\ndos  \r\n\r\n\r\nTres\n\n");
    CHECK_EQ(lines.size(), size_t{4});
    if (lines.size() == 4) {
        CHECK(lines[0].text == L"Uno" && lines[0].timeMs == -1);
        CHECK(lines[1].text == L"dos");
        CHECK(lines[2].text.empty());
        CHECK(lines[3].text == L"Tres");
    }
    CHECK(LrcLyrics::PlainLines(L"").empty());
    CHECK(LrcLyrics::PlainLines(L"\n \n").empty());
}

void TestCacheRoundTrip() {
    TEST("Lyrics: the cache keeps synced and plain lyrics, accents included, and reads old files");
    std::vector<LrcLyrics::Line> lines = {{1000, L"Corazón"}, {2500, L"Aragón 中"}};
    std::vector<LrcLyrics::Line> back;
    bool synced = false;
    CHECK(LrcLyrics::ParseCache(LrcLyrics::SerializeCache(lines, true), back, synced));
    CHECK(synced && back.size() == 2 && back[0].timeMs == 1000 && back[1].text == L"Aragón 中");

    auto plain = LrcLyrics::PlainLines(L"Son al son\n\nOtra estrofa");
    CHECK(LrcLyrics::ParseCache(LrcLyrics::SerializeCache(plain, false), back, synced));
    CHECK(!synced && back.size() == 3 && back[1].text.empty() && back[2].timeMs == -1);

    // A file written before plain lyrics existed: no marker, synced lines.
    CHECK(LrcLyrics::ParseCache(L"1000\tfirst\r\n2000\tsecond\r\n", back, synced));
    CHECK(synced && back.size() == 2 && back[1].text == L"second");
    CHECK(!LrcLyrics::ParseCache(L"", back, synced));
}

void TestCacheKeepsOffset() {
    TEST("Lyrics: the cache keeps the timing correction, and files without one read as none");
    std::vector<LrcLyrics::Line> lines = {{1000, L"uno"}, {2000, L"dos"}};
    std::vector<LrcLyrics::Line> back;
    bool synced = false;
    int offset = 99;
    CHECK(LrcLyrics::ParseCache(LrcLyrics::SerializeCache(lines, true, -1500), back, synced, &offset));
    CHECK_EQ(offset, -1500);
    CHECK(synced && back.size() == 2 && back[0].text == L"uno"); // the line is not read as lyrics
    CHECK(LrcLyrics::ParseCache(LrcLyrics::SerializeCache(lines, true), back, synced, &offset));
    CHECK_EQ(offset, 0);
    CHECK(LrcLyrics::ParseCache(L"#offset\t999999\n1000\tx\n", back, synced, &offset));
    CHECK_EQ(offset, LrcLyrics::kMaxOffsetMs); // a hand-edited file cannot push it past the limit
    CHECK(LrcLyrics::ParseCache(L"#plain\n#offset\t500\n-1\tx\n", back, synced, &offset));
    CHECK(!synced && offset == 500 && back.size() == 1);
}

void TestPositionCatchesUp() {
    TEST("Position: a playing track's reported position is moved on by the time since the report");
    // Spotify reports every 4-5 s; read as is, the position was that far behind.
    CHECK_EQ(MediaPosition::NowMs(74400, 3400, 148608, true), 77800LL);
    CHECK_EQ(MediaPosition::NowMs(74400, 3400, 148608, false), 74400LL); // paused: it is where it was
    CHECK_EQ(MediaPosition::NowMs(147000, 5000, 148608, true), 148608LL); // not past the end
    CHECK_EQ(MediaPosition::NowMs(10000, 5000, 0, true), 15000LL);        // a stream has no end
    CHECK_EQ(MediaPosition::NowMs(10000, -200, 148608, true), 10000LL);   // a report from the future
    CHECK_EQ(MediaPosition::NowMs(10000, 7LL * 60 * 60 * 1000, 0, true), 10000LL); // no report time
    CHECK_EQ(MediaPosition::NowMs(-1, 3000, 0, true), -1LL);              // no position at all
}

} // namespace

void RunLyricsTests() {
    TestPickPrefersSyncedOfTheSameLength();
    TestPickFallsBackToPlain();
    TestPickSkipsInstrumentalAndEmpty();
    TestPlainLines();
    TestCacheRoundTrip();
    TestCacheKeepsOffset();
    TestPositionCatchesUp();
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

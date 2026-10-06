// Covers what can be checked about crash handling without crashing anything: the exact line
// the log gets, when a crash is allowed to start a new copy (and when it must not, to avoid
// a loop), how the replacement's command line is built, and the pruning of old dumps. The
// handler itself - a real exception, a real dump, a real restart - is exercised by
// tools/scripts/Test-CrashHandling.ps1, which needs a process it can afford to kill.
#include "TestHarness.h"

#include "CrashHandler.h"

#include <windows.h>

#include <string>
#include <vector>

using namespace CrashHandler;

namespace {

void TestExceptionNames() {
    TEST("Crash: exception codes have names, and an unknown one still gets a line");
    CHECK_EQ(std::string(ExceptionName(EXCEPTION_ACCESS_VIOLATION)), std::string("access violation"));
    CHECK_EQ(std::string(ExceptionName(EXCEPTION_STACK_OVERFLOW)), std::string("stack overflow"));
    CHECK_EQ(std::string(ExceptionName(0xC0000374)), std::string("heap corruption"));
    CHECK_EQ(std::string(ExceptionName(kTerminateCode)),
             std::string("unhandled C++ exception (std::terminate)"));
    CHECK_EQ(std::string(ExceptionName(0x12345678)), std::string("exception"));
}

void TestRestartDecision() {
    TEST("Crash: one restart, never a loop");
    const unsigned long long minute = kRestartLoopWindowMs;

    // Switched off: never.
    CHECK(!ShouldRestart(false, false, 0));
    CHECK(!ShouldRestart(false, false, 10 * minute));

    // A copy that was started normally restarts, whenever it dies.
    CHECK(ShouldRestart(true, false, 0));
    CHECK(ShouldRestart(true, false, 5000));
    CHECK(ShouldRestart(true, false, 10 * minute));

    // A copy that was itself started by a crash and dies again within a minute is the loop
    // (it fails at startup - a damaged config, say): no second restart.
    CHECK(!ShouldRestart(true, true, 0));
    CHECK(!ShouldRestart(true, true, minute - 1));
    // Once it has run for a minute it was a real session, and a later crash restarts again.
    CHECK(ShouldRestart(true, true, minute));
    CHECK(ShouldRestart(true, true, 30 * minute));
}

void TestParseCommandLine() {
    TEST("Crash: the restart flags are found wherever they are, and only as whole words");
    StartupInfo normal = ParseCommandLine(L"\"C:\\Program Files\\EdgeDeck\\EdgeDeck.exe\"");
    CHECK(!normal.restartedAfterCrash);
    CHECK_EQ(static_cast<int>(normal.previousPid), 0);

    StartupInfo restarted = ParseCommandLine(
        L"\"C:\\Users\\x\\EdgeDeck.exe\" --restarted-after-crash --previous-pid=4812");
    CHECK(restarted.restartedAfterCrash);
    CHECK_EQ(static_cast<int>(restarted.previousPid), 4812);

    // After other arguments, and in the other order.
    StartupInfo mixed = ParseCommandLine(L"EdgeDeck.exe C:\\trig --previous-pid=77 --restarted-after-crash");
    CHECK(mixed.restartedAfterCrash);
    CHECK_EQ(static_cast<int>(mixed.previousPid), 77);

    // A flag with no pid, a pid with no digits, and text that merely contains the flag.
    CHECK(ParseCommandLine(L"EdgeDeck.exe --restarted-after-crash").restartedAfterCrash);
    CHECK_EQ(static_cast<int>(ParseCommandLine(L"EdgeDeck.exe --restarted-after-crash").previousPid), 0);
    CHECK_EQ(static_cast<int>(ParseCommandLine(L"EdgeDeck.exe --restarted-after-crash --previous-pid=abc").previousPid), 0);
    CHECK(!ParseCommandLine(L"EdgeDeck.exe x--restarted-after-crash").restartedAfterCrash);
    CHECK(!ParseCommandLine(nullptr).restartedAfterCrash);
}

void TestRestartCommandLine() {
    TEST("Crash: the replacement keeps the original arguments and carries fresh restart flags");
    wchar_t out[512];

    CHECK(BuildRestartCommandLine(L"\"C:\\My Apps\\EdgeDeck.exe\" C:\\trig", 4812, out, 512));
    CHECK(std::wstring(out) ==
          L"\"C:\\My Apps\\EdgeDeck.exe\" C:\\trig --restarted-after-crash --previous-pid=4812");

    // Restarting a copy that was itself restarted must not pile the flags up.
    wchar_t again[512];
    CHECK(BuildRestartCommandLine(out, 999, again, 512));
    CHECK(std::wstring(again) ==
          L"\"C:\\My Apps\\EdgeDeck.exe\" C:\\trig --restarted-after-crash --previous-pid=999");

    // And what it builds is what ParseCommandLine reads back.
    StartupInfo info = ParseCommandLine(again);
    CHECK(info.restartedAfterCrash);
    CHECK_EQ(static_cast<int>(info.previousPid), 999);
}

void TestRestartCommandLineLimits() {
    TEST("Crash: a command line that does not fit is refused, not truncated");
    wchar_t small[40];
    CHECK(!BuildRestartCommandLine(L"EdgeDeck.exe", 1, small, 40));
    wchar_t out[128];
    const std::wstring huge(200, L'x');
    CHECK(!BuildRestartCommandLine(huge.c_str(), 1, out, 128));
    CHECK(!BuildRestartCommandLine(nullptr, 1, out, 128));
}

void TestCrashLine() {
    TEST("Crash: the log line says what, where, when and which build");
    CrashFacts facts;
    facts.code = EXCEPTION_ACCESS_VIOLATION;
    facts.moduleName = "EdgeDeck.exe";
    facts.moduleOffset = 0x1A2B3C;
    facts.threadId = 4812;
    facts.uptimeSeconds = 931;
    facts.buildStamp = 0x6702F1A4;

    char line[256];
    const size_t length = FormatCrashLine(line, sizeof(line), facts);
    CHECK_EQ(std::string(line),
             std::string("Crash: access violation (0xC0000005) at EdgeDeck.exe+0x1A2B3C, thread 4812, "
                         "up 931 s, build 0x6702F1A4"));
    CHECK_EQ(length, std::string(line).size());

    // An address that is in no module (jumping through a bad pointer).
    facts.moduleName = nullptr;
    FormatCrashLine(line, sizeof(line), facts);
    CHECK(std::string(line).find("outside any module") != std::string::npos);

    // std::terminate reports as what it is, not as a CPU exception.
    facts.moduleName = "EdgeDeck.exe";
    facts.code = kTerminateCode;
    facts.fromTerminate = true;
    FormatCrashLine(line, sizeof(line), facts);
    CHECK(std::string(line).find("unhandled C++ exception (std::terminate)") != std::string::npos);
    CHECK(std::string(line).find("0xE0000001") != std::string::npos);
}

void TestCrashLineNeverOverruns() {
    TEST("Crash: a buffer that is too small is filled and terminated, never overrun");
    CrashFacts facts;
    facts.code = EXCEPTION_ACCESS_VIOLATION;
    facts.moduleName = "EdgeDeck.exe";

    char tiny[20];
    std::fill(std::begin(tiny), std::end(tiny), 'Z');
    const size_t length = FormatCrashLine(tiny, sizeof(tiny), facts);
    CHECK_EQ(length, sizeof(tiny) - 1);
    CHECK_EQ(static_cast<int>(tiny[sizeof(tiny) - 1]), 0); // terminated
    CHECK_EQ(std::string(tiny).size(), sizeof(tiny) - 1);

    char one[1] = {'Z'};
    CHECK_EQ(FormatCrashLine(one, 1, facts), size_t{0});
    CHECK_EQ(static_cast<int>(one[0]), 0);
    CHECK_EQ(FormatCrashLine(nullptr, 10, facts), size_t{0});
    CHECK_EQ(FormatCrashLine(tiny, 0, facts), size_t{0});
}

// A scratch folder of .dmp files whose write times are set explicitly, so "newest" is not
// decided by how fast the test happens to run.
class DumpFolder {
public:
    DumpFolder() {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        m_path = std::wstring(temp) + L"EdgeDeckCrashTest";
        CreateDirectoryW(m_path.c_str(), nullptr);
    }
    ~DumpFolder() {
        for (const std::wstring& file : m_files) DeleteFileW(file.c_str());
        RemoveDirectoryW(m_path.c_str());
    }
    // age: larger = older.
    void Add(const wchar_t* name, int age) {
        const std::wstring path = m_path + L"\\" + name;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER t;
        t.LowPart = now.dwLowDateTime;
        t.HighPart = now.dwHighDateTime;
        t.QuadPart -= static_cast<ULONGLONG>(age) * 10000000ULL * 3600; // an hour per step
        FILETIME stamp{t.LowPart, static_cast<DWORD>(t.HighPart)};
        SetFileTime(file, nullptr, nullptr, &stamp);
        CloseHandle(file);
        m_files.push_back(path);
    }
    bool Exists(const wchar_t* name) const {
        return GetFileAttributesW((m_path + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    const wchar_t* Path() const { return m_path.c_str(); }

private:
    std::wstring m_path;
    std::vector<std::wstring> m_files;
};

void TestPruneKeepsNewest() {
    TEST("Crash: only the newest dumps are kept, and other files are left alone");
    DumpFolder folder;
    folder.Add(L"a.dmp", 1);  // newest
    folder.Add(L"b.dmp", 2);
    folder.Add(L"c.dmp", 3);
    folder.Add(L"d.dmp", 4);
    folder.Add(L"e.dmp", 5);
    folder.Add(L"f.dmp", 6);
    folder.Add(L"g.dmp", 7);  // oldest
    folder.Add(L"notes.txt", 9);

    PruneDumps(folder.Path(), 5);
    CHECK(folder.Exists(L"a.dmp"));
    CHECK(folder.Exists(L"e.dmp"));
    CHECK(!folder.Exists(L"f.dmp"));
    CHECK(!folder.Exists(L"g.dmp"));
    CHECK(folder.Exists(L"notes.txt")); // not a dump: not ours to delete

    // Nothing to do when there are no more than the limit; safe on a folder that is not there.
    PruneDumps(folder.Path(), 5);
    CHECK(folder.Exists(L"e.dmp"));
    PruneDumps(L"C:\\this\\folder\\does\\not\\exist", 5);
    PruneDumps(nullptr, 5);
}

} // namespace

void RunCrashTests() {
    TestExceptionNames();
    TestRestartDecision();
    TestParseCommandLine();
    TestRestartCommandLine();
    TestRestartCommandLineLimits();
    TestCrashLine();
    TestCrashLineNeverOverruns();
    TestPruneKeepsNewest();
}

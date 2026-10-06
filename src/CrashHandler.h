#pragma once

#include <windows.h>

#include <cstddef>

// What to leave behind when EdgeDeck dies, and getting it going again.
//
// A resident utility that crashes and says nothing is the worst kind: it is simply gone,
// the log has no trace of why, and it stays gone until the next sign-in. This installs a
// handler that records the crash - one line in the log with the exception and where it
// happened, and a small minidump to open in a debugger - and, unless switched off, starts
// a fresh copy once.
//
// The handler runs in a process whose heap and CRT may be damaged (the last crash this
// project fixed was heap corruption), so nothing that runs *during* a crash allocates:
// every buffer is on the stack or static, and the only calls are Win32 ones. Everything
// that has to be decided is a pure function below, so it can be tested without crashing
// anything.
//
// What it cannot see: __fastfail (a CRT or WinRT fail-fast) bypasses every handler by
// design, and a hard kill from outside (Task Manager, taskkill) is not a crash.
namespace CrashHandler {

// Installs the handlers and records where the log and the dumps go. Call once, first thing:
// anything that crashes before this is invisible. Also keeps the five newest dumps and
// reserves stack for the handler, so a stack overflow can still be reported.
void Install();

// Whether a crash starts a fresh copy. Defaults to on; set from the config once it is read.
void SetRestartEnabled(bool enabled);

// --- a copy that was started by a crash -------------------------------------------------

struct StartupInfo {
    bool restartedAfterCrash = false; // --restarted-after-crash was on the command line
    DWORD previousPid = 0;            // --previous-pid=N: the copy that crashed
};

// Pure. Looks for the two flags a restart adds, anywhere on the command line.
StartupInfo ParseCommandLine(const wchar_t* commandLine);

// Waits (at most ten seconds) for the crashed copy to be gone, so the single-instance lock
// it held is free by the time this one asks for it. A no-op for a normal start.
void WaitForPrevious(const StartupInfo& info);

// --- pure pieces, exposed for the tests -------------------------------------------------

// "access violation", "stack overflow", ... or "exception" for a code with no friendlier name.
const char* ExceptionName(DWORD code);

// One restart, not a loop: a copy that was itself started by a crash and dies again within
// a minute is not restarted a second time, which would otherwise spin forever on something
// that fails at startup - a damaged config, say.
bool ShouldRestart(bool restartEnabled, bool startedAfterCrash, unsigned long long uptimeMs);
inline constexpr unsigned long long kRestartLoopWindowMs = 60 * 1000;

// The command line for the replacement copy: the original one, minus any restart flags it
// already carried, plus fresh ones. Returns false if it does not fit in `capacity`.
bool BuildRestartCommandLine(const wchar_t* original, DWORD crashedPid, wchar_t* out, size_t capacity);

struct CrashFacts {
    DWORD code = 0;
    const char* moduleName = nullptr;   // "EdgeDeck.exe"; null if the address is in no module
    unsigned long long moduleOffset = 0; // address minus the module's base
    DWORD threadId = 0;
    unsigned long long uptimeSeconds = 0;
    DWORD buildStamp = 0;               // the linker's timestamp: identifies which build this was
    bool fromTerminate = false;         // std::terminate, not a CPU exception
};

// "Crash: access violation (0xC0000005) at EdgeDeck.exe+0x1A2B3C, thread 4812, up 931 s, build
// 0x6702F1A4". Never allocates; truncates rather than overruns. Returns the length written.
size_t FormatCrashLine(char* buffer, size_t capacity, const CrashFacts& facts);

// Deletes all but the `keep` newest .dmp files in `folder`. Missing folder: nothing to do.
// Allocates, so it runs at startup, never during a crash.
void PruneDumps(const wchar_t* folder, int keep);

// The synthetic exception code std::terminate raises so it flows through the same handler.
inline constexpr DWORD kTerminateCode = 0xE0000001;

} // namespace CrashHandler

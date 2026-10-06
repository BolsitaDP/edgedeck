#include "CrashHandler.h"

#include "Diagnostics.h"

#include <intrin.h> // _ReturnAddress
#include <dbghelp.h> // types only: MiniDumpWriteDump is looked up when needed, so dbghelp.dll is never loaded in a healthy run

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace CrashHandler {
namespace {

constexpr wchar_t kFlagRestarted[] = L"--restarted-after-crash";
constexpr wchar_t kFlagPid[] = L"--previous-pid=";
constexpr int kDumpsToKeep = 5;
constexpr size_t kCommandCapacity = 2048;

// Everything the handler needs is prepared here, at startup, so that nothing has to be
// built (and nothing allocated) while the process is dying.
wchar_t g_logPath[MAX_PATH];
wchar_t g_dumpFolder[MAX_PATH];
wchar_t g_exePath[MAX_PATH];
ULONGLONG g_startTick = 0;
DWORD g_buildStamp = 0;
bool g_startedAfterCrash = false;
std::atomic<bool> g_restartEnabled{true};
volatile LONG g_inHandler = 0;
volatile bool g_fromTerminate = false;

void CopyTo(wchar_t* destination, size_t capacity, const std::wstring& source) {
    if (source.size() + 1 > capacity) {
        destination[0] = L'\0'; // too long to hold: the feature that needs it switches itself off
        return;
    }
    wmemcpy(destination, source.c_str(), source.size() + 1);
}

// The linker stamps every build with the time it was linked, which is what tells two
// builds' crashes apart - and which debug symbols to open a dump with.
DWORD ReadBuildStamp() {
    const auto* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
}

// Same shape as Diagnostics' lines ("HH:MM:SS level text"), but written with Win32 alone:
// Diagnostics allocates and takes a lock, neither of which is safe here.
void AppendLog(const char* level, const char* text) {
    if (!g_logPath[0]) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char line[640];
    int length = snprintf(line, sizeof(line), "%02d:%02d:%02d %-5s %s\r\n", now.wHour, now.wMinute,
                          now.wSecond, level, text);
    if (length <= 0) return;
    if (length >= static_cast<int>(sizeof(line))) { // truncated: still end the line properly
        length = static_cast<int>(sizeof(line)) - 1;
        line[length - 2] = '\r';
        line[length - 1] = '\n';
    }
    HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(file);
}

bool ModuleOf(ULONGLONG address, char* name, size_t capacity, ULONGLONG& offset) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module) ||
        !module) {
        return false;
    }
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    const wchar_t* base = path + length;
    while (base > path && base[-1] != L'\\') --base;
    if (WideCharToMultiByte(CP_UTF8, 0, base, -1, name, static_cast<int>(capacity), nullptr, nullptr) == 0) {
        return false;
    }
    offset = address - reinterpret_cast<ULONGLONG>(module);
    return true;
}

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                          PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                          PMINIDUMP_CALLBACK_INFORMATION);

// A small dump - threads, stacks, modules - not the whole address space: enough to open in
// Visual Studio or WinDbg next to the matching .pdb and see where it stopped.
bool WriteDump(EXCEPTION_POINTERS* info, wchar_t* pathOut, size_t pathCapacity) {
    if (!g_dumpFolder[0]) return false;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (swprintf_s(pathOut, pathCapacity, L"%s\\EdgeDeck-%04d%02d%02d-%02d%02d%02d-%lu.dmp", g_dumpFolder,
                   now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                   GetCurrentProcessId()) < 0) {
        return false;
    }

    // Looked up now, not at startup: in a healthy run dbghelp.dll is never loaded at all.
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) return false;
    const auto writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!writeDump) return false;

    HANDLE file = CreateFileW(pathOut, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo |
                                                          MiniDumpWithUnloadedModules);
    const BOOL ok = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                              info ? &exception : nullptr, nullptr, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

bool Relaunch() {
    if (!g_exePath[0]) return false;
    wchar_t command[kCommandCapacity];
    if (!BuildRestartCommandLine(GetCommandLineW(), GetCurrentProcessId(), command, kCommandCapacity)) {
        return false;
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(g_exePath, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info) {
    // A crash inside the handler itself: there is nothing left to try, end the process.
    if (InterlockedCompareExchange(&g_inHandler, 1, 0) != 0) return EXCEPTION_EXECUTE_HANDLER;

    const ULONGLONG uptimeMs = GetTickCount64() - g_startTick;

    CrashFacts facts;
    char moduleName[64];
    facts.code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    if (info && info->ExceptionRecord &&
        ModuleOf(reinterpret_cast<ULONGLONG>(info->ExceptionRecord->ExceptionAddress), moduleName,
                 sizeof(moduleName), facts.moduleOffset)) {
        facts.moduleName = moduleName;
    }
    facts.threadId = GetCurrentThreadId();
    facts.uptimeSeconds = uptimeMs / 1000;
    facts.buildStamp = g_buildStamp;
    facts.fromTerminate = g_fromTerminate;

    // The line first: it is the one thing that must survive even if everything after it fails.
    char text[512];
    FormatCrashLine(text, sizeof(text), facts);
    AppendLog("error", text);

    wchar_t dumpPath[MAX_PATH];
    if (WriteDump(info, dumpPath, MAX_PATH)) {
        char narrow[MAX_PATH * 3];
        if (WideCharToMultiByte(CP_UTF8, 0, dumpPath, -1, narrow, sizeof(narrow), nullptr, nullptr) > 0) {
            char message[MAX_PATH * 3 + 40];
            snprintf(message, sizeof(message), "Crash: dump written to %s", narrow);
            AppendLog("error", message);
        }
    } else {
        char message[80];
        snprintf(message, sizeof(message), "Crash: no dump written (error %lu)", GetLastError());
        AppendLog("error", message);
    }

    if (ShouldRestart(g_restartEnabled.load(), g_startedAfterCrash, uptimeMs)) {
        AppendLog("error", Relaunch() ? "Crash: started a new copy" : "Crash: could not start a new copy");
    } else {
        AppendLog("error", "Crash: not restarting (switched off, or it crashed again within a minute of a restart)");
    }

    // End the process here: no Windows Error Reporting dialog, no second chance.
    return EXCEPTION_EXECUTE_HANDLER;
}

// std::terminate - an exception nothing caught, including one that escaped a coroutine - has
// no EXCEPTION_POINTERS of its own. They are made up here so that it gets the same log line,
// dump and restart as a CPU exception, instead of a silent abort().
//
// The obvious way, RaiseException from this handler, does not work: the CRT treats an
// exception raised while it is terminating as fatal and fails fast (0xC0000409) before any
// handler runs. So the filter's logic is called directly, with a record and a context that
// describe this call.
void TerminateHandler() {
    g_fromTerminate = true;

    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = kTerminateCode;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = _ReturnAddress(); // whoever called std::terminate
    EXCEPTION_POINTERS pointers{&record, &context};

    UnhandledFilter(&pointers);
    // The exit code says which kind of end this was, as an access violation's does.
    TerminateProcess(GetCurrentProcess(), kTerminateCode);
}

bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t'; }

// Finds `flag` as a whole token (preceded by the start or whitespace). npos if absent.
size_t FindFlag(std::wstring_view line, std::wstring_view flag, size_t from = 0) {
    for (size_t at = line.find(flag, from); at != std::wstring_view::npos; at = line.find(flag, at + 1)) {
        if (at == 0 || IsSpace(line[at - 1])) return at;
    }
    return std::wstring_view::npos;
}

// Removes every " <flag>..." token (flag, plus its value if it ends in '=') from `text`, in place.
void StripFlag(wchar_t* text, const wchar_t* flag) {
    const size_t flagLength = wcslen(flag);
    for (;;) {
        const size_t at = FindFlag(text, flag);
        if (at == std::wstring_view::npos) return;
        size_t end = at + flagLength;
        if (flag[flagLength - 1] == L'=') {
            while (text[end] && !IsSpace(text[end])) ++end;
        }
        size_t start = at;
        while (start > 0 && IsSpace(text[start - 1])) --start;
        wmemmove(text + start, text + end, wcslen(text + end) + 1);
    }
}

} // namespace

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case 0xC0000374: return "heap corruption";
        case 0xC0000409: return "stack buffer overrun";
        case kTerminateCode: return "unhandled C++ exception (std::terminate)";
        default: return "exception";
    }
}

bool ShouldRestart(bool restartEnabled, bool startedAfterCrash, unsigned long long uptimeMs) {
    if (!restartEnabled) return false;
    return !(startedAfterCrash && uptimeMs < kRestartLoopWindowMs);
}

StartupInfo ParseCommandLine(const wchar_t* commandLine) {
    StartupInfo info;
    if (!commandLine) return info;
    const std::wstring_view line(commandLine);

    info.restartedAfterCrash = FindFlag(line, kFlagRestarted) != std::wstring_view::npos;

    const size_t at = FindFlag(line, kFlagPid);
    if (at != std::wstring_view::npos) {
        const wchar_t* digits = commandLine + at + wcslen(kFlagPid);
        wchar_t* end = nullptr;
        const unsigned long pid = wcstoul(digits, &end, 10);
        if (end != digits) info.previousPid = static_cast<DWORD>(pid);
    }
    return info;
}

bool BuildRestartCommandLine(const wchar_t* original, DWORD crashedPid, wchar_t* out, size_t capacity) {
    constexpr size_t kRoomForFlags = 64;
    if (!original || !out || capacity <= kRoomForFlags) return false;
    const size_t length = wcslen(original);
    if (length + kRoomForFlags >= capacity) return false;

    wmemcpy(out, original, length + 1);
    StripFlag(out, kFlagRestarted);
    StripFlag(out, kFlagPid);
    return swprintf_s(out + wcslen(out), capacity - wcslen(out), L" %s %s%lu", kFlagRestarted, kFlagPid,
                      crashedPid) > 0;
}

size_t FormatCrashLine(char* buffer, size_t capacity, const CrashFacts& facts) {
    if (!buffer || capacity == 0) return 0;
    const char* name = facts.fromTerminate ? ExceptionName(kTerminateCode) : ExceptionName(facts.code);
    int written;
    if (facts.moduleName) {
        written = snprintf(buffer, capacity, "Crash: %s (0x%08lX) at %s+0x%llX, thread %lu, up %llu s, build 0x%08lX",
                           name, facts.code, facts.moduleName, facts.moduleOffset, facts.threadId,
                           facts.uptimeSeconds, facts.buildStamp);
    } else {
        written = snprintf(buffer, capacity, "Crash: %s (0x%08lX) at an address outside any module, thread %lu, up %llu s, build 0x%08lX",
                           name, facts.code, facts.threadId, facts.uptimeSeconds, facts.buildStamp);
    }
    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }
    return std::min(static_cast<size_t>(written), capacity - 1);
}

void PruneDumps(const wchar_t* folder, int keep) {
    if (!folder || !*folder || keep < 0) return;
    struct Entry {
        std::wstring name;
        FILETIME written;
    };
    std::vector<Entry> dumps;

    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((std::wstring(folder) + L"\\*.dmp").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) dumps.push_back({found.cFileName, found.ftLastWriteTime});
    } while (FindNextFileW(search, &found));
    FindClose(search);

    if (dumps.size() <= static_cast<size_t>(keep)) return;
    std::sort(dumps.begin(), dumps.end(), [](const Entry& a, const Entry& b) {
        return CompareFileTime(&a.written, &b.written) > 0; // newest first
    });
    for (size_t i = static_cast<size_t>(keep); i < dumps.size(); ++i) {
        DeleteFileW((std::wstring(folder) + L"\\" + dumps[i].name).c_str());
    }
}

void SetRestartEnabled(bool enabled) { g_restartEnabled.store(enabled); }

void WaitForPrevious(const StartupInfo& info) {
    if (!info.restartedAfterCrash || info.previousPid == 0) return;
    HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, info.previousPid);
    if (!previous) return; // already gone, which is what is wanted
    WaitForSingleObject(previous, 10000);
    CloseHandle(previous);
}

void Install() {
    g_startTick = GetTickCount64();
    g_buildStamp = ReadBuildStamp();
    g_startedAfterCrash = ParseCommandLine(GetCommandLineW()).restartedAfterCrash;

    const std::wstring log = Diagnostics::LogPath();
    if (!log.empty()) {
        CopyTo(g_logPath, MAX_PATH, log);
        const std::wstring folder = log.substr(0, log.find_last_of(L'\\')) + L"\\crashes";
        CreateDirectoryW(folder.c_str(), nullptr);
        CopyTo(g_dumpFolder, MAX_PATH, folder);
        PruneDumps(folder.c_str(), kDumpsToKeep);
    }
    const DWORD exeLength = GetModuleFileNameW(nullptr, g_exePath, MAX_PATH);
    if (exeLength == 0 || exeLength >= MAX_PATH) g_exePath[0] = L'\0';

    // A stack overflow leaves the thread with no stack to run a handler on. Reserving some
    // is what lets one be reported at all (this thread only - the UI thread, where it matters).
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);

    SetUnhandledExceptionFilter(UnhandledFilter);
    std::set_terminate(TerminateHandler);
}

} // namespace CrashHandler

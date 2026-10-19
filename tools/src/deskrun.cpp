// Runs a program on a desktop of its own that nobody is looking at, and waits for it.
// usage: deskrun <program> [arguments...]
//
// Windows can have several desktops in a window station; only one takes input and is shown. A
// window opened on another one cannot take the focus, cover anything or be clicked by accident, so
// a tool that only needs to *draw* a window - settingsharness shot=..., which captures it with
// PrintWindow - can run while someone keeps working. The desktop is closed again when the
// program exits. Exit code: the program's, or 100+ when it could not be started.
#include <windows.h>

#include <cstdio>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: deskrun <program> [arguments...]\n");
        return 100;
    }

    // The command line as given, quoting each argument that needs it.
    std::wstring command;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) command += L' ';
        const std::wstring arg = argv[i];
        if (arg.find_first_of(L" \t\"") == std::wstring::npos && !arg.empty()) {
            command += arg;
        } else {
            command += L'"';
            for (wchar_t c : arg) {
                if (c == L'"') command += L'\\';
                command += c;
            }
            command += L'"';
        }
    }

    const wchar_t* name = L"EdgeDeckOffscreen";
    HDESK desktop = CreateDesktopW(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) {
        fwprintf(stderr, L"deskrun: CreateDesktop failed (%lu)\n", GetLastError());
        return 101;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    std::wstring desktopName = name;
    startup.lpDesktop = desktopName.data();
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        fwprintf(stderr, L"deskrun: could not start %ls (%lu)\n", argv[1], GetLastError());
        CloseDesktop(desktop);
        return 102;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseDesktop(desktop);
    return static_cast<int>(code);
}

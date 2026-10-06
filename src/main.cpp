#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include "App.h"
#include "CrashHandler.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // First, so that anything below that goes wrong leaves a line in the log and a dump
    // instead of an app that is simply gone.
    CrashHandler::Install();

    // Per-Monitor-V2 is the modern, manifest-free way to opt in to sharp
    // rendering under Windows DPI scaling (125%, 150%, ...). Must happen
    // before any window is created.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // A copy started because the previous one crashed has to wait for it to be gone: the
    // crashed process still holds the single-instance lock below until it has exited.
    const CrashHandler::StartupInfo startup = CrashHandler::ParseCommandLine(GetCommandLineW());
    CrashHandler::WaitForPrevious(startup);

    // A second copy would duplicate the always-on-top windows and hotkeys.
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\EdgeDeck.SingleInstance");
    if (!instanceMutex) {
        MessageBoxW(nullptr, L"EdgeDeck could not create its single-instance lock.",
                    L"EdgeDeck", MB_ICONERROR | MB_OK);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(instanceMutex);
        return 0;
    }

    // Needed for the Shell.Application COM object used by Actions::ShowDesktopToggle.
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        MessageBoxW(nullptr, L"EdgeDeck could not initialize Windows COM services.",
                    L"EdgeDeck", MB_ICONERROR | MB_OK);
        CloseHandle(instanceMutex);
        return 1;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc); // needed once, before the settings window's comctl32 children

    int exitCode = 1;
    {
        App app;
        if (startup.restartedAfterCrash) app.NoteRestartedAfterCrash();
        if (!app.Create(hInstance)) {
            MessageBoxW(nullptr, L"EdgeDeck failed to initialize its windows.", L"EdgeDeck",
                        MB_ICONERROR | MB_OK);
        } else {
            exitCode = App::RunMessageLoop();
        }
    }

    CoUninitialize();
    CloseHandle(instanceMutex);
    return exitCode;
}

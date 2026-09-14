#include <windows.h>
#include <objbase.h>
#include "EdgeWindow.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Per-Monitor-V2 is the modern, manifest-free way to opt in to sharp
    // rendering under Windows DPI scaling (125%, 150%, ...). Must happen
    // before any window is created.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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

    int exitCode = 1;
    {
        EdgeWindow app;
        if (!app.Create(hInstance)) {
            MessageBoxW(nullptr, L"EdgeDeck failed to initialize its windows.", L"EdgeDeck",
                        MB_ICONERROR | MB_OK);
        } else {
            exitCode = EdgeWindow::RunMessageLoop();
        }
    }

    CoUninitialize();
    CloseHandle(instanceMutex);
    return exitCode;
}

#include <windows.h>
#include <objbase.h>
#include "EdgeWindow.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Per-Monitor-V2 is the modern, manifest-free way to opt in to sharp
    // rendering under Windows DPI scaling (125%, 150%, ...). Must happen
    // before any window is created.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Needed for the Shell.Application COM object used by Actions::ShowDesktopToggle.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    EdgeWindow app;
    if (!app.Create(hInstance)) {
        MessageBoxW(nullptr, L"EdgeDeck failed to initialize its windows.", L"EdgeDeck",
                    MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    int exitCode = EdgeWindow::RunMessageLoop();

    CoUninitialize();
    return exitCode;
}

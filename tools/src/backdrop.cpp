// A plain solid-colour topmost window, so a translucent / per-pixel-alpha window
// placed over it shows exactly what is (or is not) transparent.
// usage: backdrop x y w h seconds [rrggbb]
#include <windows.h>
#include <cstdlib>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    if (argc < 6) return 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const int x = _wtoi(argv[1]), y = _wtoi(argv[2]), w = _wtoi(argv[3]), h = _wtoi(argv[4]);
    const int seconds = _wtoi(argv[5]);
    COLORREF color = RGB(255, 0, 0);
    if (argc >= 7) {
        unsigned v = wcstoul(argv[6], nullptr, 16);
        color = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"EdgeDeckTestBackdrop";
    wc.hbrBackground = CreateSolidBrush(color);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                                L"backdrop", WS_POPUP, x, y, w, h, nullptr, nullptr, wc.hInstance,
                                nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    DWORD end = GetTickCount() + seconds * 1000;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }
    return 0;
}

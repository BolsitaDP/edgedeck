// A borderless, NOT topmost window of a given size that becomes the foreground window, to stand in for
// a full-screen game or video. Not topmost on purpose: the tabs are topmost and must stay above it.
// usage: fullscreenwin x y w h seconds [rrggbb]
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    if (argc < 6) return 1;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int x = _wtoi(argv[1]), y = _wtoi(argv[2]), w = _wtoi(argv[3]), h = _wtoi(argv[4]);
    const int seconds = _wtoi(argv[5]);
    COLORREF color = RGB(16, 16, 16);
    if (argc >= 7) {
        unsigned v = wcstoul(argv[6], nullptr, 16);
        color = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"EdgeDeckTestFullscreen";
    wc.hbrBackground = CreateSolidBrush(color);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"fullscreen test", WS_POPUP | WS_VISIBLE, x, y, w, h,
                                nullptr, nullptr, wc.hInstance, nullptr);
    // Windows refuses SetForegroundWindow from a background process unless a key was just pressed.
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    SetForegroundWindow(hwnd);
    Sleep(150);
    printf("foreground=%d\n", GetForegroundWindow() == hwnd ? 1 : 0);

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

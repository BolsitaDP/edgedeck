// Runs the real App in this process (no single-instance lock, so it can sit beside the user's own
// copy) and lets a script inject problem reports into its first tab by dropping trig*.txt files:
// the file's text is posted exactly as a widget's ReportProblem would. A file named quit.txt exits.
//
// usage: apptest <trigger-folder>
#include "App.h"
#include "PanelWidget.h"

#include <commctrl.h>
#include <objbase.h>
#include <windows.h>

#include <string>

static HWND g_tab = nullptr;

static BOOL CALLBACK FindTab(HWND hwnd, LPARAM) {
    wchar_t cls[64];
    GetClassNameW(hwnd, cls, 64);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && wcscmp(cls, kTabClassName) == 0) {
        g_tab = hwnd;
        return FALSE;
    }
    return TRUE;
}

static std::wstring ReadUtf8(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    char buf[1024];
    DWORD read = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &read, nullptr);
    CloseHandle(f);
    int n = MultiByteToWideChar(CP_UTF8, 0, buf, static_cast<int>(read), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf, static_cast<int>(read), out.data(), n);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    return out;
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    const std::wstring dir = argc > 1 ? argv[1] : L".";

    int exitCode = 1;
    {
        App app;
        if (!app.Create(GetModuleHandleW(nullptr))) return 2;
        EnumWindows(FindTab, 0);

        for (bool running = true; running;) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) running = false;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\trig*.txt").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    const std::wstring path = dir + L"\\" + fd.cFileName;
                    const std::wstring text = ReadUtf8(path);
                    DeleteFileW(path.c_str());
                    PanelWidget::ReportProblem(g_tab, text);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (GetFileAttributesW((dir + L"\\quit.txt").c_str()) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileW((dir + L"\\quit.txt").c_str());
                app.RequestExit();
            }
            Sleep(15);
        }
        exitCode = 0;
    }
    CoUninitialize();
    return exitCode;
}

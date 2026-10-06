// Opens just the Settings window (no tabs, no hotkeys, no single-instance mutex), so it can be
// inspected while the user's own EdgeDeck keeps running.
// usage: settingsharness <follow|dark|light> <seconds> [drop|drop=N] [flip] [dpi=N]
//   drop=N : open the Nth combo box's list (1 widget type, 2 monitor, 3 edge, 4 theme)
//   dpi=N : after creation, send the window the WM_DPICHANGED a monitor at N DPI would, so the
//           layout can be checked at other scales without changing the user's display settings.
// Always prints a layout report: controls outside the client area, overlapping controls, and
// text that is wider (or, for the footer, taller) than the control that holds it.
#include "Config.h"
#include "Renderer.h"
#include "SettingsWindow.h"

#include <commctrl.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

struct Child {
    HWND hwnd;
    RECT rect; // client coordinates
    std::wstring cls;
    std::wstring text;
    LONG style;
};

static std::vector<Child> CollectChildren(HWND parent) {
    std::vector<Child> out;
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        Child ch{};
        ch.hwnd = c;
        GetWindowRect(c, &ch.rect);
        MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&ch.rect), 2);
        wchar_t cls[64], text[512];
        GetClassNameW(c, cls, 64);
        GetWindowTextW(c, text, 512);
        ch.cls = cls;
        ch.text = text;
        ch.style = static_cast<LONG>(GetWindowLongPtrW(c, GWL_STYLE));
        out.push_back(ch);
    }
    return out;
}

static int LayoutReport(HWND hwnd, UINT dpi) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const auto kids = CollectChildren(hwnd);
    int problems = 0;
    printf("layout @%u dpi: client %ldx%ld, %zu controls\n", dpi, client.right, client.bottom, kids.size());

    for (const Child& c : kids) {
        if (c.rect.left < 0 || c.rect.top < 0 || c.rect.right > client.right || c.rect.bottom > client.bottom) {
            printf("  OUTSIDE client: %ls '%ls' (%ld,%ld)-(%ld,%ld)\n", c.cls.c_str(), c.text.c_str(),
                   c.rect.left, c.rect.top, c.rect.right, c.rect.bottom);
            ++problems;
        }
    }
    for (size_t i = 0; i < kids.size(); ++i) {
        for (size_t j = i + 1; j < kids.size(); ++j) {
            RECT inter;
            if (IntersectRect(&inter, &kids[i].rect, &kids[j].rect)) {
                printf("  OVERLAP: %ls '%ls' x %ls '%ls' (%ldx%ld px)\n", kids[i].cls.c_str(),
                       kids[i].text.c_str(), kids[j].cls.c_str(), kids[j].text.c_str(),
                       inter.right - inter.left, inter.bottom - inter.top);
                ++problems;
            }
        }
    }

    // Text that cannot fit its control.
    for (const Child& c : kids) {
        const bool isStatic = _wcsicmp(c.cls.c_str(), L"Static") == 0;
        const bool isButton = _wcsicmp(c.cls.c_str(), L"Button") == 0;
        if ((!isStatic && !isButton) || c.text.empty()) continue;

        HDC dc = GetDC(c.hwnd);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(c.hwnd, WM_GETFONT, 0, 0));
        HGDIOBJ old = SelectObject(dc, font);
        const int boxW = c.rect.right - c.rect.left;
        const int boxH = c.rect.bottom - c.rect.top;

        if (c.text.find(L'\n') != std::wstring::npos) {
            RECT calc{0, 0, boxW, 0};
            DrawTextW(dc, c.text.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK);
            if (calc.bottom > boxH) {
                printf("  TEXT TOO TALL: footer needs %ld px, has %d\n", calc.bottom, boxH);
                ++problems;
            }
            // Longest line against the width.
            size_t start = 0;
            while (start <= c.text.size()) {
                size_t end = c.text.find(L'\n', start);
                std::wstring line = c.text.substr(start, end == std::wstring::npos ? end : end - start);
                SIZE sz{};
                GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &sz);
                if (sz.cx > boxW) {
                    printf("  LINE TOO WIDE: '%ls' needs %ld px, has %d\n", line.c_str(), sz.cx, boxW);
                    ++problems;
                }
                if (end == std::wstring::npos) break;
                start = end + 1;
            }
        } else {
            SIZE sz{};
            GetTextExtentPoint32W(dc, c.text.c_str(), static_cast<int>(c.text.size()), &sz);
            // A button needs its own margins; a check box also needs its glyph.
            int needed = sz.cx;
            if (isButton) needed += MulDiv((c.style & BS_AUTOCHECKBOX) == BS_AUTOCHECKBOX ? 24 : 12, dpi, 96);
            if (needed > boxW) {
                printf("  TEXT TOO WIDE: %ls '%ls' needs %d px, has %d\n", c.cls.c_str(), c.text.c_str(),
                       needed, boxW);
                ++problems;
            }
        }
        SelectObject(dc, old);
        ReleaseDC(c.hwnd, dc);
    }
    printf("  -> %d problem(s)\n", problems);
    return problems;
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    setvbuf(stdout, nullptr, _IONBF, 0);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    const wchar_t* mode = argc > 1 ? argv[1] : L"follow";
    const int seconds = argc > 2 ? _wtoi(argv[2]) : 8;
    bool drop = false, flip = false;
    int dropIndex = 1;
    UINT simDpi = 0;
    for (int i = 3; i < argc; ++i) {
        if (wcscmp(argv[i], L"drop") == 0) drop = true;
        else if (wcsncmp(argv[i], L"drop=", 5) == 0) { drop = true; dropIndex = _wtoi(argv[i] + 5); }
        else if (wcscmp(argv[i], L"flip") == 0) flip = true;
        else if (wcsncmp(argv[i], L"dpi=", 4) == 0) simDpi = static_cast<UINT>(_wtoi(argv[i] + 4));
    }

    Config::SetCurrentThemeMode(wcscmp(mode, L"dark") == 0    ? ThemeMode::Dark
                                : wcscmp(mode, L"light") == 0 ? ThemeMode::Light
                                                              : ThemeMode::Follow);
    PanelTheme::Refresh();

    std::vector<TabSettings> tabs = {
        {WidgetType::Displays, 0.20f, 26.0f, 76.0f, 300.0f},
        {WidgetType::Media, 0.38f, 26.0f, 76.0f, 320.0f},
        {WidgetType::Volume, 0.58f, 26.0f, 76.0f, 300.0f},
        {WidgetType::Brightness, 0.80f, 26.0f, 76.0f, 300.0f},
    };

    SettingsWindow settings;
    if (!settings.Create(GetModuleHandleW(nullptr), tabs, [](const std::vector<TabSettings>&) {})) {
        return 1;
    }
    HWND hwnd = settings.Hwnd();

    printf("window DPI as created: %u\n", GetDpiForWindow(hwnd));
    LayoutReport(hwnd, GetDpiForWindow(hwnd));

    if (simDpi) {
        // The size a monitor at this scale would give the window: logical client area scaled,
        // grown by the frame at that scale - the rectangle Windows would propose.
        RECT cur{};
        GetWindowRect(hwnd, &cur);
        // The logical client size is read back from the window rather than written down here, so
        // a change to the layout cannot leave this harness asking for a window of the old size.
        RECT client{};
        GetClientRect(hwnd, &client);
        const UINT createdDpi = GetDpiForWindow(hwnd);
        const int logicalW = MulDiv(client.right, 96, static_cast<int>(createdDpi));
        const int logicalH = MulDiv(client.bottom, 96, static_cast<int>(createdDpi));
        RECT r{0, 0, MulDiv(logicalW, simDpi, 96), MulDiv(logicalH, simDpi, 96)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE,
                                 WS_EX_DLGMODALFRAME, simDpi);
        RECT suggested{cur.left, cur.top, cur.left + (r.right - r.left), cur.top + (r.bottom - r.top)};
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(simDpi, simDpi), reinterpret_cast<LPARAM>(&suggested));
        printf("after simulated change to %u DPI:\n", simDpi);
        LayoutReport(hwnd, simDpi);
    }

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    if (drop) {
        // The dropdown-th combo box in tab order: 1 is the widget type, 2 the monitor, 3 the edge.
        struct Pick { int remaining; } pick{dropIndex};
        EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
            wchar_t cls[32];
            GetClassNameW(child, cls, 32);
            if (_wcsicmp(cls, L"ComboBox") != 0) return TRUE;
            auto* p = reinterpret_cast<Pick*>(param);
            if (--p->remaining > 0) return TRUE;
            SendMessageW(child, CB_SHOWDROPDOWN, TRUE, 0);
            return FALSE;
        }, reinterpret_cast<LPARAM>(&pick));
    }

    bool flipped = false;
    DWORD flipAt = GetTickCount() + 1800;
    DWORD end = GetTickCount() + seconds * 1000;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (!settings.Hwnd() || !IsDialogMessageW(settings.Hwnd(), &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (flip && !flipped && GetTickCount() >= flipAt) {
            flipped = true;
            Config::SetCurrentThemeMode(ThemeMode::Light);
            SendMessageW(settings.Hwnd(), WM_THEMECHANGED, 0, 0);
        }
        Sleep(15);
    }
    return 0;
}

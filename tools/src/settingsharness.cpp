// Opens just the Settings window (no tabs, no hotkeys, no single-instance mutex), so it can be
// inspected while the user's own EdgeDeck keeps running.
// usage: settingsharness <follow|dark|light> <seconds> [drop|drop=N] [flip] [dpi=N] [window=actions]
//                        [dropfiles=X,Y,path|path|...] [lang=en|es]
//   lang=es : the windows in that language (the layout of each language is checked separately)
//   window=actions : host the quick-actions editor instead of Settings, and report its layout
//   dropfiles=X,Y,paths : drag those files from "Explorer" onto the point
//           (X,Y) of the editor's client area, given in logical 96-DPI units - a real OLE drag, so
//           it goes through the same lookup of the window under the pointer a user's drag does -
//           then print the list, the status line, and what Save would keep. On Settings the
//           first tab is a Recent files one, and what is printed is the folder box.
//   drop=N : open the Nth combo box's list (1 widget type, 2 monitor, 3 edge, 4 theme)
//   dpi=N : after creation, send the window the WM_DPICHANGED a monitor at N DPI would, so the
//           layout can be checked at other scales without changing the user's display settings.
// Always prints a layout report: controls outside the client area, overlapping controls, and
// text that is wider (or, for the footer, taller) than the control that holds it.
#include "ActionsWindow.h"
#include "Config.h"
#include "Renderer.h"
#include "SettingsWindow.h"
#include "Strings.h"

#include <commctrl.h>
#include <ole2.h>
#include <shlobj.h>
#include <shobjidl.h>

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
            if (isStatic && needed > boxW) {
                // A label wraps. It only fits if the wrapped text still fits the box's height - which
                // is what separates a paragraph meant to wrap from a one-line label that has run out
                // of room and would lose its second line.
                RECT calc{0, 0, boxW, 0};
                DrawTextW(dc, c.text.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK);
                if (calc.bottom > boxH) {
                    printf("  TEXT TOO TALL: Static '%ls' wraps to %ld px, has %d\n", c.text.c_str(),
                           calc.bottom, boxH);
                    ++problems;
                }
            } else if (needed > boxW) {
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

// ---- a drag of files from Explorer, performed with no mouse button ----------------------------

// The drag source: lets the loop run a few turns so the target is entered and hovered, then drops.
struct DropSource final : IDropSource {
    int calls = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *object = static_cast<IDropSource*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }  // lives on the stack
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD) override {
        if (escape) return DRAGDROP_S_CANCEL;
        return ++calls < 4 ? S_OK : DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

static IDataObject* DataObjectFor(const std::vector<std::wstring>& paths) {
    std::vector<PCIDLIST_ABSOLUTE> pidls;
    for (const std::wstring& p : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            pidls.push_back(pidl);
        }
    }
    IDataObject* data = nullptr;
    IShellItemArray* items = nullptr;
    if (!pidls.empty() &&
        SUCCEEDED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(pidls.size()), pidls.data(),
                                                    &items)) &&
        items) {
        items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
        items->Release();
    }
    for (PCIDLIST_ABSOLUTE pidl : pidls) CoTaskMemFree(const_cast<LPITEMIDLIST>(pidl));
    return data;
}

static POINT g_dropPoint{};
static void CALLBACK NudgePointer(HWND, UINT, UINT_PTR, DWORD) {
    // The drag loop is driven by pointer movement; a one-pixel step either way keeps it turning.
    static bool flip = false;
    flip = !flip;
    SetCursorPos(g_dropPoint.x + (flip ? 1 : 0), g_dropPoint.y);
}

static void DropFilesOnto(HWND window, int logicalX, int logicalY, const std::vector<std::wstring>& paths) {
    const UINT dpi = GetDpiForWindow(window);
    POINT at{MulDiv(logicalX, static_cast<int>(dpi), 96), MulDiv(logicalY, static_cast<int>(dpi), 96)};
    ClientToScreen(window, &at);
    g_dropPoint = at;
    SetCursorPos(at.x, at.y);

    IDataObject* data = DataObjectFor(paths);
    if (!data) {
        printf("drop: could not make a data object for %zu path(s)\n", paths.size());
        return;
    }
    DropSource source;
    DWORD effect = DROPEFFECT_NONE;
    const UINT_PTR timer = SetTimer(nullptr, 0, 30, NudgePointer);
    const HRESULT hr = DoDragDrop(data, &source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    KillTimer(nullptr, timer);
    data->Release();
    printf("drop: hr=0x%08lX effect=%lu (DROPEFFECT_NONE=0, COPY=1, MOVE=2, LINK=4)\n",
           static_cast<unsigned long>(hr), static_cast<unsigned long>(effect));
}

// What the editor shows after a drop: its list, its status line, and (by pressing Save) the rows
// it would keep.
static void ReportEditor(HWND window) {
    struct State { std::vector<std::wstring> items; std::vector<std::wstring> status; HWND save = nullptr; };
    State state;
    EnumChildWindows(window, [](HWND child, LPARAM param) -> BOOL {
        auto* s = reinterpret_cast<State*>(param);
        wchar_t cls[32], text[512];
        GetClassNameW(child, cls, 32);
        GetWindowTextW(child, text, 512);
        if (_wcsicmp(cls, L"ListBox") == 0) {
            const int count = static_cast<int>(SendMessageW(child, LB_GETCOUNT, 0, 0));
            for (int i = 0; i < count; ++i) {
                wchar_t item[256] = {};
                SendMessageW(child, LB_GETTEXT, i, reinterpret_cast<LPARAM>(item));
                s->items.push_back(item);
            }
        } else if (_wcsicmp(cls, L"Static") == 0 &&
                   (wcsncmp(text, L"Added", 5) == 0 || wcsncmp(text, L"The list", 8) == 0)) {
            s->status.push_back(text);
        } else if (_wcsicmp(cls, L"Button") == 0 && wcscmp(text, L"Save") == 0) {
            s->save = child;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));

    printf("list: %zu item(s)\n", state.items.size());
    for (const std::wstring& item : state.items) printf("  item: %ls\n", item.c_str());
    for (const std::wstring& line : state.status) printf("status: %ls\n", line.c_str());
    if (state.save) SendMessageW(state.save, BM_CLICK, 0, 0); // the save callback prints each action
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    setvbuf(stdout, nullptr, _IONBF, 0);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    const wchar_t* mode = argc > 1 ? argv[1] : L"follow";
    const int seconds = argc > 2 ? _wtoi(argv[2]) : 8;
    bool drop = false, flip = false, hostActions = false;
    int dropIndex = 1;
    UINT simDpi = 0;
    bool dropFiles = false;
    int dropX = 0, dropY = 0;
    std::vector<std::wstring> dropPaths;
    for (int i = 3; i < argc; ++i) {
        if (wcsncmp(argv[i], L"dropfiles=", 10) == 0) {
            // dropfiles=X,Y,path|path
            dropFiles = true;
            wchar_t* rest = argv[i] + 10;
            dropX = static_cast<int>(wcstol(rest, &rest, 10));
            if (*rest == L',') ++rest;
            dropY = static_cast<int>(wcstol(rest, &rest, 10));
            if (*rest == L',') ++rest;
            std::wstring list = rest;
            size_t start = 0;
            while (start <= list.size()) {
                const size_t bar = list.find(L'|', start);
                const std::wstring one = list.substr(start, bar == std::wstring::npos ? bar : bar - start);
                if (!one.empty()) dropPaths.push_back(one);
                if (bar == std::wstring::npos) break;
                start = bar + 1;
            }
        }
        else if (wcscmp(argv[i], L"drop") == 0) drop = true;
        else if (wcsncmp(argv[i], L"drop=", 5) == 0) { drop = true; dropIndex = _wtoi(argv[i] + 5); }
        else if (wcscmp(argv[i], L"flip") == 0) flip = true;
        else if (wcscmp(argv[i], L"window=actions") == 0) hostActions = true;
        else if (wcsncmp(argv[i], L"dpi=", 4) == 0) simDpi = static_cast<UINT>(_wtoi(argv[i] + 4));
    }

    // With OLE already running when the controls are made, an edit box registers a drop target of
    // its own for text - the situation FileDrop::Zone exists for. `ole` asks for that.
    for (int i = 3; i < argc; ++i) {
        if (wcscmp(argv[i], L"ole") == 0) OleInitialize(nullptr);
        if (wcsncmp(argv[i], L"lang=", 5) == 0) Strings::SetCurrent(Strings::FromCode(argv[i] + 5));
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

    // A drop on Settings is a folder for a Recent files tab, which is the one selected.
    if (dropFiles && !hostActions) tabs.insert(tabs.begin(), {WidgetType::RecentFiles, 0.5f, 26.0f, 76.0f, 300.0f});

    SettingsWindow settings;
    ActionsWindow editor;
    if (hostActions) {
        // A full list, so the buttons that depend on the count are in their usual state.
        std::vector<QuickAction> list = QuickActions::Defaults();
        list.push_back({L"M\u00FAsica", L"%USERPROFILE%\\M\u00FAsica\\lista.m3u", L"--shuffle"});
        if (!editor.Create(GetModuleHandleW(nullptr), list, [](std::vector<QuickAction> kept) {
                printf("saved: %zu action(s)\n", kept.size());
                for (const QuickAction& a : kept) {
                    printf("  action: %ls | %ls | %ls\n", a.name.c_str(), a.target.c_str(), a.arguments.c_str());
                }
            })) {
            return 1;
        }
    } else if (!settings.Create(GetModuleHandleW(nullptr), tabs, [](const std::vector<TabSettings>&) {})) {
        return 1;
    }
    const auto windowHandle = [&] { return hostActions ? editor.Hwnd() : settings.Hwnd(); };
    HWND hwnd = windowHandle();

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

    if (dropFiles) {
        // Let the window finish appearing, then drag onto it and report what it did with that.
        const DWORD settle = GetTickCount() + 400;
        MSG pending;
        while (GetTickCount() < settle) {
            while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&pending);
                DispatchMessageW(&pending);
            }
            Sleep(10);
        }
        DropFilesOnto(hwnd, dropX, dropY, dropPaths);
        if (hostActions) {
            ReportEditor(hwnd);
        } else {
            wchar_t folder[600] = {};
            GetWindowTextW(GetDlgItem(hwnd, 119), folder, 600); // the Recent folder box
            printf("folder box: %ls\n", folder);
        }
    }

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
            if (!windowHandle() || !IsDialogMessageW(windowHandle(), &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (flip && !flipped && GetTickCount() >= flipAt) {
            flipped = true;
            Config::SetCurrentThemeMode(ThemeMode::Light);
            SendMessageW(windowHandle(), WM_THEMECHANGED, 0, 0);
        }
        Sleep(15);
    }
    return 0;
}

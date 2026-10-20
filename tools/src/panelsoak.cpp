// Opens and closes real panels over and over and writes what the process holds after each round:
// the way to tell memory that settles from memory that grows with use. The tabs are the product's
// own Tab, Renderer and widgets, as in panelshot; each round opens every tab's panel in turn
// (RequestOpen, as a keyboard shortcut does), lets its data arrive, and closes it with Escape.
//
// usage: panelsoak <out.csv> [rounds=N] [open=MS] [closed=MS] [every=N] [widgets=A,B,...]
//   widgets: QuickActions, Media, Volume, Brightness, Lyrics, Displays, Recent (default: all)
//
// Run it through deskrun: the windows live on a desktop nobody sees, so it runs while someone
// works - nothing appears, nothing takes the focus, the cursor is not touched. LOCALAPPDATA is a
// scratch folder, so it reads no one's settings and writes no one's log. The data is this
// machine's (what is playing, the devices, the monitors), and a Lyrics tab asks LRCLIB once per
// track, then reads its cache.
//
// The CSV has one line per sample: round, private and working-set KB, handles, GDI and USER
// objects, threads.
#include "App.h"
#include "BrightnessWidget.h"
#include "Config.h"
#include "DisplayWidget.h"
#include "LyricsWidget.h"
#include "MediaWidget.h"
#include "QuickActionsWidget.h"
#include "RecentFilesWidget.h"
#include "Renderer.h"
#include "Tab.h"
#include "VolumeWidget.h"

#include <objbase.h>
#include <psapi.h>
#include <share.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<PanelWidget> MakeWidget(const std::wstring& name) {
    if (name == L"Media") return std::make_unique<MediaWidget>();
    if (name == L"Volume") return std::make_unique<VolumeWidget>();
    if (name == L"Brightness") return std::make_unique<BrightnessWidget>();
    if (name == L"Lyrics") return std::make_unique<LyricsWidget>();
    if (name == L"Displays") return std::make_unique<DisplayWidget>();
    if (name == L"Recent") return std::make_unique<RecentFilesWidget>();
    if (name == L"QuickActions") return std::make_unique<QuickActionsWidget>();
    return nullptr;
}

void Pump(DWORD milliseconds) {
    const ULONGLONG end = GetTickCount64() + milliseconds;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

// This thread's panel windows that are not in `known` yet.
std::vector<HWND> NewPanels(const std::vector<HWND>& known) {
    struct Search {
        const std::vector<HWND>* known;
        std::vector<HWND> found;
    } search{&known, {}};
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(param);
            wchar_t name[64];
            if (GetClassNameW(hwnd, name, 64) && wcscmp(name, kPanelClassName) == 0 &&
                std::find(s.known->begin(), s.known->end(), hwnd) == s.known->end()) {
                s.found.push_back(hwnd);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

int ThreadCount() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    int count = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
    }
    CloseHandle(snapshot);
    return count;
}

void Sample(FILE* out, int round) {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    const DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    fprintf(out, "%d,%zu,%zu,%lu,%lu,%lu,%d\n", round, memory.PrivateUsage / 1024, memory.WorkingSetSize / 1024,
            handles, gdi, user, ThreadCount());
    fflush(out);
}

std::vector<std::wstring> Split(const std::wstring& list) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(L',', start);
        const std::wstring part = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        if (!part.empty()) parts.push_back(part);
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return parts;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: panelsoak <out.csv> [rounds=N] [open=MS] [closed=MS] [every=N] [widgets=A,B,...]\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::error_code ec;
    const auto scratch = std::filesystem::temp_directory_path(ec) / (L"edgedeck-panelsoak-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(scratch, ec);
    SetEnvironmentVariableW(L"LOCALAPPDATA", scratch.c_str());

    int rounds = 100;
    DWORD openMs = 700;
    DWORD closedMs = 300;
    int every = 5;
    std::vector<std::wstring> names = {L"QuickActions", L"Media", L"Volume", L"Brightness",
                                       L"Lyrics",       L"Displays", L"Recent"};
    for (int i = 2; i < argc; ++i) {
        if (wcsncmp(argv[i], L"rounds=", 7) == 0) rounds = _wtoi(argv[i] + 7);
        if (wcsncmp(argv[i], L"open=", 5) == 0) openMs = static_cast<DWORD>(_wtoi(argv[i] + 5));
        if (wcsncmp(argv[i], L"closed=", 7) == 0) closedMs = static_cast<DWORD>(_wtoi(argv[i] + 7));
        if (wcsncmp(argv[i], L"every=", 6) == 0) every = std::max(1, _wtoi(argv[i] + 6));
        if (wcsncmp(argv[i], L"widgets=", 8) == 0) names = Split(argv[i] + 8);
    }

    // Shared for reading, so a long run can be looked at while it goes.
    FILE* out = _wfsopen(argv[1], L"w", _SH_DENYWR);
    if (!out) return 3;
    fprintf(out, "round,private_kb,ws_kb,handles,gdi,user,threads\n");

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    Config::SetCurrentThemeMode(ThemeMode::Dark);
    PanelTheme::Refresh();
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!Tab::RegisterClasses(instance)) return 4;

    int result = 0;
    {
        std::vector<std::unique_ptr<Tab>> tabs;
        std::vector<HWND> panels;
        for (size_t i = 0; i < names.size(); ++i) {
            std::unique_ptr<PanelWidget> widget = MakeWidget(names[i]);
            if (!widget) {
                fwprintf(stderr, L"panelsoak: unknown widget '%ls'\n", names[i].c_str());
                return 2;
            }
            TabConfig config;
            config.verticalRatio = names.size() == 1 ? 0.5f : 0.1f + 0.8f * static_cast<float>(i) / (names.size() - 1);
            auto tab = std::make_unique<Tab>(nullptr, config, std::move(widget));
            if (!tab->Create(instance)) return 5;
            const std::vector<HWND> created = NewPanels(panels);
            if (created.size() != 1) {
                fwprintf(stderr, L"panelsoak: expected one new panel window, found %zu\n", created.size());
                return 5;
            }
            panels.push_back(created.front());
            tabs.push_back(std::move(tab));
        }
        Pump(500);
        Sample(out, 0);

        for (int round = 1; round <= rounds; ++round) {
            for (size_t i = 0; i < tabs.size(); ++i) {
                tabs[i]->RequestOpen();
                Pump(openMs);
                SendMessageW(panels[i], WM_KEYDOWN, VK_ESCAPE, 0);
                Pump(closedMs);
            }
            if (round % every == 0 || round == rounds) Sample(out, round);
        }
        wprintf(L"panelsoak: %d rounds of %zu panels\n", rounds, tabs.size());
    }
    Tab::UnregisterClasses();
    CoUninitialize();
    fclose(out);
    std::filesystem::remove_all(scratch, ec);
    return result;
}

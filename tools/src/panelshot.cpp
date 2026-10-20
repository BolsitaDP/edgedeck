// Pictures one widget's panel as EdgeDeck draws it, without a screen: a real tab and panel (the
// product's Tab, Renderer and widget) are made, the panel opened and left a moment for its data
// to arrive, and the pixels it drew are composed over a plain colour and written as a BMP.
//
// usage: panelshot <widget> <dark|light> <out.bmp> [lang=en|es] [bg=RRGGBB] [wait=MS] [width=N]
//                  [recent=FOLDER] [pinned]
//   widget: QuickActions, Media, Volume, Brightness, Lyrics, Displays, Recent
//
// Run it through deskrun and the windows live on a desktop nobody sees: no tab appears on the
// screen in use, nothing takes the focus and the cursor is not touched - so it runs while someone
// works, and makes the README's pictures. LOCALAPPDATA is pointed at a scratch folder first, so it
// reads no one's settings and writes no one's log. The data is this machine's: what is playing,
// the audio devices, the monitors.
#include "BrightnessWidget.h"
#include "Config.h"
#include "DisplayWidget.h"
#include "LyricsWidget.h"
#include "MediaWidget.h"
#include "QuickActionsWidget.h"
#include "RecentFilesWidget.h"
#include "Renderer.h"
#include "Strings.h"
#include "Tab.h"
#include "VolumeWidget.h"

#include <objbase.h>

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

// Premultiplied BGRA over an opaque colour, written as a 24-bit bottom-up BMP.
bool WriteComposited(const wchar_t* path, const std::vector<std::uint8_t>& bgra, UINT w, UINT h, COLORREF bg) {
    const UINT stride = (w * 3 + 3) & ~3u;
    std::vector<std::uint8_t> out(static_cast<size_t>(stride) * h, 0);
    for (UINT y = 0; y < h; ++y) {
        for (UINT x = 0; x < w; ++x) {
            const std::uint8_t* p = &bgra[(static_cast<size_t>(y) * w + x) * 4];
            const unsigned inverse = 255 - p[3];
            std::uint8_t* q = &out[static_cast<size_t>(h - 1 - y) * stride + x * 3];
            q[0] = static_cast<std::uint8_t>(p[0] + (GetBValue(bg) * inverse + 127) / 255);
            q[1] = static_cast<std::uint8_t>(p[1] + (GetGValue(bg) * inverse + 127) / 255);
            q[2] = static_cast<std::uint8_t>(p[2] + (GetRValue(bg) * inverse + 127) / 255);
        }
    }
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = static_cast<LONG>(w);
    info.biHeight = static_cast<LONG>(h);
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biCompression = BI_RGB;
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(out.size());
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return false;
    fwrite(&file, sizeof(file), 1, f);
    fwrite(&info, sizeof(info), 1, f);
    fwrite(out.data(), out.size(), 1, f);
    fclose(f);
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        fwprintf(stderr, L"usage: panelshot <widget> <dark|light> <out.bmp> [lang=en|es] [bg=RRGGBB] [wait=MS] "
                         L"[width=N] [recent=FOLDER] [pinned]\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // No one's settings or log: a scratch folder of its own.
    std::error_code ec;
    const auto scratch = std::filesystem::temp_directory_path(ec) / (L"edgedeck-panelshot-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(scratch, ec);
    SetEnvironmentVariableW(L"LOCALAPPDATA", scratch.c_str());

    const std::wstring widgetName = argv[1];
    const bool dark = wcscmp(argv[2], L"light") != 0;
    const wchar_t* outPath = argv[3];
    COLORREF bg = dark ? RGB(0x20, 0x2A, 0x38) : RGB(0xD8, 0xE2, 0xEC);
    DWORD wait = 1500;
    float width = 0.0f;
    bool pinned = false;
    for (int i = 4; i < argc; ++i) {
        if (wcsncmp(argv[i], L"lang=", 5) == 0) Strings::SetCurrent(Strings::FromCode(argv[i] + 5));
        if (wcsncmp(argv[i], L"bg=", 3) == 0) {
            const unsigned long rgb = wcstoul(argv[i] + 3, nullptr, 16);
            bg = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        }
        if (wcsncmp(argv[i], L"wait=", 5) == 0) wait = static_cast<DWORD>(_wtoi(argv[i] + 5));
        if (wcsncmp(argv[i], L"width=", 6) == 0) width = static_cast<float>(_wtoi(argv[i] + 6));
        if (wcsncmp(argv[i], L"recent=", 7) == 0) Config::SetRecentFolder(argv[i] + 7);
        if (wcscmp(argv[i], L"pinned") == 0) pinned = true;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    Config::SetCurrentThemeMode(dark ? ThemeMode::Dark : ThemeMode::Light);
    PanelTheme::Refresh();

    std::unique_ptr<PanelWidget> widget = MakeWidget(widgetName);
    if (!widget) {
        fwprintf(stderr, L"panelshot: unknown widget '%ls'\n", widgetName.c_str());
        return 2;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!Tab::RegisterClasses(instance)) return 4;
    TabConfig config;
    if (width > 0.0f) config.panelWidth = width;
    int result = 5;
    {
        Tab tab(nullptr, config, std::move(widget));
        if (tab.Create(instance)) {
            tab.RequestOpen();
            Pump(wait); // the slide, and the widget's first answers (icons, sessions, devices)
            std::vector<std::uint8_t> pixels;
            UINT w = 0;
            UINT h = 0;
            if (tab.SnapshotPanel(pixels, w, h, !pinned) && WriteComposited(outPath, pixels, w, h, bg)) {
                wprintf(L"panel %ls: %ux%u -> %ls\n", widgetName.c_str(), w, h, outPath);
                result = 0;
            } else {
                fwprintf(stderr, L"panelshot: no picture of the %ls panel\n", widgetName.c_str());
            }
        }
    }
    Tab::UnregisterClasses();
    CoUninitialize();
    std::filesystem::remove_all(scratch, ec);
    return result;
}

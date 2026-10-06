#include "Config.h"

#include <windows.h>

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <string>

namespace {

// The config file is a machine-written format, not prose: it must round-trip a
// decimal point regardless of the user's regional settings, and it must not
// change meaning if anything ever calls std::locale::global. Both the stream
// and the number parsing are therefore pinned to the classic locale.
std::wifstream OpenRead(const std::wstring& path) {
    std::wifstream file(path);
    file.imbue(std::locale::classic());
    return file;
}

// One process-wide "C" locale for the parser. Created once (locale_t objects
// are not cheap and this is on the config path) and deliberately never freed:
// it lives for the life of the process, which is exactly as long as it is
// needed.
_locale_t invariantLocale() {
    static _locale_t locale = _create_locale(LC_ALL, "C");
    return locale;
}

// Parses a float without throwing and without being locale-sensitive. Returns
// false for anything that is not a complete, finite number, so "abc", "12abc"
// and "1e9999" are all rejected instead of silently becoming 0 or inf.
bool ParseFloat(const std::wstring& text, float& out) {
    if (text.empty()) return false;
    std::wstring trimmed = text;
    const size_t begin = trimmed.find_first_not_of(L" \t");
    if (begin == std::wstring::npos) return false;
    trimmed = trimmed.substr(begin);
    while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t' ||
                                trimmed.back() == L'\r')) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) return false;

    wchar_t* end = nullptr;
    errno = 0;
    const double value = _wcstod_l(trimmed.c_str(), &end, invariantLocale());    if (end == trimmed.c_str() || *end != L'\0') return false;
    if (errno == ERANGE) return false;
    if (!std::isfinite(value)) return false;

    out = static_cast<float>(value);
    return true;
}

const wchar_t* WidgetTypeToString(WidgetType type) {
    switch (type) {
        case WidgetType::Media: return L"Media";
        case WidgetType::Brightness: return L"Brightness";
        case WidgetType::Lyrics: return L"Lyrics";
        case WidgetType::Volume: return L"Volume";
        case WidgetType::Displays: return L"Displays";
        default: return L"QuickActions";
    }
}

WidgetType WidgetTypeFromString(const std::wstring& s) {
    // "Spotify" is accepted for configs saved before the media widget became
    // app-agnostic.
    if (s == L"Media" || s == L"Spotify") return WidgetType::Media;
    if (s == L"Brightness") return WidgetType::Brightness;
    if (s == L"Lyrics") return WidgetType::Lyrics;
    if (s == L"Volume") return WidgetType::Volume;
    if (s == L"Displays") return WidgetType::Displays;
    return WidgetType::QuickActions;
}

std::vector<TabSettings> DefaultTabs() {
    return {
        {WidgetType::QuickActions, 0.14f, 26.0f, 76.0f, 300.0f},
        {WidgetType::Media, 0.38f, 26.0f, 76.0f, 320.0f},
        {WidgetType::Volume, 0.58f, 26.0f, 76.0f, 300.0f},
        {WidgetType::Brightness, 0.80f, 26.0f, 76.0f, 300.0f},
    };
}

} // namespace

namespace Config {

const wchar_t* ThemeModeToString(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Dark: return L"Dark";
        case ThemeMode::Light: return L"Light";
        default: return L"Follow";
    }
}

ThemeMode ThemeModeFromString(const std::wstring& s) {
    if (s == L"Dark") return ThemeMode::Dark;
    if (s == L"Light") return ThemeMode::Light;
    return ThemeMode::Follow; // Follow, and anything unrecognised
}

namespace {
// Held here so LoadOrDefault can pick up [settings] as a side effect without
// changing its signature, and so Save can write the current value without the
// caller having to thread it through.
ThemeMode g_themeMode = ThemeMode::Follow;
std::wstring g_tvMonitor;
bool g_fullscreenGuard = true;
bool g_restartAfterCrash = true;

// EDID ids are upper case ("SAM7A08"); a hand-edited value should match whatever
// case it was typed in.
std::wstring Upper(std::wstring text) {
    for (wchar_t& c : text) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    return text;
}
} // namespace

ThemeMode CurrentThemeMode() { return g_themeMode; }
void SetCurrentThemeMode(ThemeMode mode) { g_themeMode = mode; }

std::wstring DisplayTvMonitor() { return g_tvMonitor; }
void SetDisplayTvMonitor(const std::wstring& id) { g_tvMonitor = Upper(id); }

bool FullscreenGuard() { return g_fullscreenGuard; }
void SetFullscreenGuard(bool on) { g_fullscreenGuard = on; }

bool RestartAfterCrash() { return g_restartAfterCrash; }
void SetRestartAfterCrash(bool on) { g_restartAfterCrash = on; }

std::wstring FilePath() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";

    const std::wstring dir = std::wstring(buf) + L"\\EdgeDeck";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return L"";
    }
    return dir + L"\\config.txt";
}

std::vector<TabSettings> LoadOrDefault() {
    std::vector<TabSettings> result;
    const std::wstring path = FilePath();
    if (path.empty()) return DefaultTabs();

    std::wifstream file = OpenRead(path);
    if (!file) return DefaultTabs();

    g_tvMonitor.clear(); // re-read below if the file still names one
    g_fullscreenGuard = true; // likewise: absent means the default
    g_restartAfterCrash = true;
    TabSettings current;
    bool inTab = false;
    bool inSettings = false;

    auto flush = [&] {
        if (!inTab) return;
        ConfigLimits::Clamp(current);
        result.push_back(current);
    };

    std::wstring line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        if (line == L"[tab]") {
            flush();
            current = TabSettings{};
            inTab = true;
            inSettings = false;
            continue;
        }
        if (line == L"[settings]") {
            flush();
            inTab = false;
            inSettings = true;
            continue;
        }

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || (!inTab && !inSettings)) continue;
        const std::wstring key = line.substr(0, eq);
        const std::wstring value = line.substr(eq + 1);

        if (inSettings) {
            if (key == L"theme") g_themeMode = ThemeModeFromString(value);
            if (key == L"tvMonitor") g_tvMonitor = Upper(value);
            // Only an explicit "off" turns it off; anything unreadable keeps the safe default.
            if (key == L"fullscreenGuard" && (value == L"0" || value == L"false")) {
                g_fullscreenGuard = false;
            }
            if (key == L"restartAfterCrash" && (value == L"0" || value == L"false")) {
                g_restartAfterCrash = false;
            }
            continue;
        }

        if (key == L"widget") {
            current.widgetType = WidgetTypeFromString(value);
        } else if (key == L"verticalRatio") {
            ParseFloat(value, current.verticalRatio); // keeps the default on garbage
        } else if (key == L"tabWidth") {
            ParseFloat(value, current.tabWidth);
        } else if (key == L"tabHeight") {
            ParseFloat(value, current.tabHeight);
        } else if (key == L"panelWidth") {
            ParseFloat(value, current.panelWidth);
        }
        // An unknown key is ignored, which is what makes adding one later a
        // compatible change.
    }
    flush();

    // An empty but perfectly valid file (the user removed every tab) is honoured
    // as "no tabs"; a file we could not make sense of falls back to defaults.
    return result.empty() ? DefaultTabs() : result;
}

bool Save(const std::vector<TabSettings>& tabs) {
    const std::wstring path = FilePath();
    if (path.empty()) return false;

    const std::wstring temp = path + L".tmp";
    {
        std::wofstream file(temp, std::ios::trunc);
        if (!file) return false;
        file.imbue(std::locale::classic());

        // Globals first, in their own section. Written even when every value is
        // the default, so the file says what it means rather than what was
        // changed.
        file << L"[settings]\n";
        file << L"theme=" << ThemeModeToString(g_themeMode) << L"\n";
        if (!g_tvMonitor.empty()) file << L"tvMonitor=" << g_tvMonitor << L"\n";
        file << L"fullscreenGuard=" << (g_fullscreenGuard ? L"1" : L"0") << L"\n";
        file << L"restartAfterCrash=" << (g_restartAfterCrash ? L"1" : L"0") << L"\n";

        for (const auto& t : tabs) {
            file << L"[tab]\n";
            file << L"widget=" << WidgetTypeToString(t.widgetType) << L"\n";
            file << L"verticalRatio=" << t.verticalRatio << L"\n";
            file << L"tabWidth=" << t.tabWidth << L"\n";
            file << L"tabHeight=" << t.tabHeight << L"\n";
            file << L"panelWidth=" << t.panelWidth << L"\n";
        }
        file.flush();
        if (!file) {
            file.close();
            DeleteFileW(temp.c_str());
            return false;
        }
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

} // namespace Config

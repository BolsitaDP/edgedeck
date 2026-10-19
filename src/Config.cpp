#include "Config.h"

#include "RecentFiles.h"
#include "Strings.h"

#include <windows.h>

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <string>

namespace {

// The config file is UTF-8. It used to be read and written through wide file streams pinned to
// the classic locale, which round-trips ASCII and nothing else - fine while every value was a
// number or an id, and wrong the moment a quick action is called "Música" or points into a folder
// with an accent in it. The bytes are decoded and encoded here, explicitly.
//
// The file is also a machine-written format: it must round-trip a decimal point whatever the
// user's regional settings, and must not change meaning if anything ever calls
// std::locale::global. The text is therefore parsed and formatted through classic-locale streams
// and the number parsing below.
// Whether the bytes hold at least one well-formed UTF-8 sequence of two or more bytes - a lead byte
// followed by as many continuation bytes as it announces. In text from a single-byte code page an
// accented letter is one byte between plain ASCII, which never forms one.
bool HasUtf8Sequence(const char* text, int length) {
    for (int i = 0; i < length; ++i) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const int extra = (lead >= 0xC2 && lead <= 0xDF) ? 1 : (lead >= 0xE0 && lead <= 0xEF) ? 2
                        : (lead >= 0xF0 && lead <= 0xF4)   ? 3
                                                           : 0;
        if (extra == 0 || i + extra >= length) continue; // not a lead byte, or cut off by the end
        bool whole = true;
        for (int k = 1; k <= extra && whole; ++k) whole = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
        if (whole) return true;
    }
    return false;
}

bool ReadUtf8File(const std::wstring& path, std::wstring& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    // A settings file is a few hundred bytes. Far more than that is not one, and is not read.
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = bytes.empty() || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    bytes.resize(read);

    size_t start = 0;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        start = 3; // a byte order mark, which an editor may have added
    }
    const char* text = bytes.data() + start;
    const int textLength = static_cast<int>(bytes.size() - start);
    // Bytes that are not UTF-8 are a file an editor saved in the system's code page ("ANSI", what
    // older Notepads wrote): read that way, an accented name keeps its accent instead of becoming
    // a replacement character - which the next Save would then write for good. Only when nothing
    // in it is UTF-8, though: a UTF-8 file with one damaged byte read as ANSI would turn every
    // other accent into two characters ("MÃºsica"), so it stays UTF-8 with one character lost.
    UINT codePage = CP_UTF8;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, textLength, nullptr, 0);
    if (length <= 0 && textLength > 0) {
        // Either way a strict reading failed, so the length is asked again without being strict.
        if (!HasUtf8Sequence(text, textLength)) codePage = CP_ACP;
        length = MultiByteToWideChar(codePage, 0, text, textLength, nullptr, 0);
    }
    out.assign(static_cast<size_t>(length > 0 ? length : 0), L'\0');
    if (length > 0) MultiByteToWideChar(codePage, 0, text, textLength, out.data(), length);
    return true;
}

// Whether the text has anything in it but blank lines. A NUL counts: a file of zeros is what a
// power cut can leave behind, and it is not an empty file.
bool HasContent(const std::wstring& text) {
    return text.find_first_not_of(L" \t\r\n") != std::wstring::npos;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    std::string bytes;
    if (!text.empty()) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                               nullptr, 0, nullptr, nullptr);
        if (length <= 0) return false;
        bytes.assign(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), length,
                            nullptr, nullptr);
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const BOOL flushed = ok && FlushFileBuffers(file);
    CloseHandle(file);
    return ok && flushed && written == bytes.size();
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
    const double value = _wcstod_l(trimmed.c_str(), &end, invariantLocale());
    if (end == trimmed.c_str() || *end != L'\0') return false;
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
        case WidgetType::RecentFiles: return L"Recent";
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
    if (s == L"Recent") return WidgetType::RecentFiles;
    return WidgetType::QuickActions;
}

const wchar_t* EdgeToString(ScreenEdge edge) {
    return edge == ScreenEdge::Left ? L"left" : L"right";
}

// Anything unrecognised is the right edge, where every tab used to live.
ScreenEdge EdgeFromString(const std::wstring& s) {
    return (s == L"left" || s == L"Left") ? ScreenEdge::Left : ScreenEdge::Right;
}

// What a first start shows: the three tabs that work on any Windows machine. Brightness is not one -
// it needs external monitors with DDC/CI, and on a laptop it would open to "not available" - and
// the others are a choice the user makes in Settings.
std::vector<TabSettings> DefaultTabs() {
    return {
        {WidgetType::QuickActions, 0.25f, 26.0f, 76.0f, 300.0f},
        {WidgetType::Media, 0.45f, 26.0f, 76.0f, 320.0f},
        {WidgetType::Volume, 0.65f, 26.0f, 76.0f, 300.0f},
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
std::wstring g_mainMonitor;
bool g_fullscreenGuard = true;
bool g_restartAfterCrash = true;
int g_openDelayMs = 0;
bool g_startupHintShown = false;
std::wstring g_recentFolder;             // empty means Downloads, resolved where it is used
Shortcuts::Set g_shortcuts;
std::vector<DisplayLayouts::Layout> g_layouts; // empty means the automatic ones
bool g_layoutsCarried = false;
std::vector<QuickAction> g_quickActions; // empty means "the defaults", resolved by the accessor
LoadProblem g_loadProblem = LoadProblem::None;
std::wstring g_backupPath;

// Copies a file LoadOrDefault could not fully read to config.backup.txt beside it, before a Save
// replaces it, and remembers what was found.
void KeepAside(const std::wstring& path, LoadProblem problem) {
    g_loadProblem = problem;
    const size_t slash = path.find_last_of(L'\\');
    const std::wstring backup = path.substr(0, slash + 1) + kBackupFileName;
    if (CopyFileW(path.c_str(), backup.c_str(), FALSE)) g_backupPath = backup;
}

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

std::wstring DisplayMainMonitor() { return g_mainMonitor; }
void SetDisplayMainMonitor(const std::wstring& id) { g_mainMonitor = Upper(id); }

bool FullscreenGuard() { return g_fullscreenGuard; }
void SetFullscreenGuard(bool on) { g_fullscreenGuard = on; }

bool RestartAfterCrash() { return g_restartAfterCrash; }
void SetRestartAfterCrash(bool on) { g_restartAfterCrash = on; }

int OpenDelayMs() { return g_openDelayMs; }
void SetOpenDelayMs(int ms) { g_openDelayMs = ms < 0 ? 0 : (ms > kMaxOpenDelayMs ? kMaxOpenDelayMs : ms); }

std::vector<QuickAction> QuickActionList() {
    return g_quickActions.empty() ? QuickActions::Defaults() : g_quickActions;
}
void SetQuickActionList(std::vector<QuickAction> list) {
    g_quickActions = QuickActions::Sanitize(std::move(list));
}

std::vector<DisplayLayouts::Layout> DisplayLayoutList() { return g_layouts; }
void SetDisplayLayoutList(std::vector<DisplayLayouts::Layout> layouts) {
    g_layouts = DisplayLayouts::Sanitize(std::move(layouts));
}
bool DisplayLayoutsCarried() { return g_layoutsCarried; }
void SetDisplayLayoutsCarried(bool carried) { g_layoutsCarried = carried; }

Shortcuts::Set ShortcutSet() { return g_shortcuts; }
void SetShortcutSet(const Shortcuts::Set& set) { g_shortcuts = set; }

std::wstring RecentFolder() { return g_recentFolder; }
void SetRecentFolder(const std::wstring& folder) { g_recentFolder = RecentFiles::SanitizeFolder(folder); }

bool StartupHintShown() { return g_startupHintShown; }
void SetStartupHintShown(bool shown) { g_startupHintShown = shown; }

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

LoadProblem LastLoadProblem() { return g_loadProblem; }
std::wstring LastBackupPath() { return g_backupPath; }

std::vector<TabSettings> LoadOrDefault() {
    std::vector<TabSettings> result;
    g_loadProblem = LoadProblem::None;
    g_backupPath.clear();
    const std::wstring path = FilePath();
    if (path.empty()) return DefaultTabs();

    std::wstring contents;
    if (!ReadUtf8File(path, contents)) {
        // No file is a first start; a file that cannot be read (far too big, locked, unreadable)
        // is the user's settings, which the next Save would otherwise replace with the defaults.
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            KeepAside(path, LoadProblem::Unreadable);
        }
        return DefaultTabs();
    }
    std::wistringstream file(contents);
    file.imbue(std::locale::classic());

    g_tvMonitor.clear(); // re-read below if the file still names one
    g_mainMonitor.clear();
    g_fullscreenGuard = true; // likewise: absent means the default
    g_restartAfterCrash = true;
    g_openDelayMs = 0;
    g_startupHintShown = false;
    g_recentFolder.clear();
    g_shortcuts = Shortcuts::Set{};
    g_layouts.clear();
    g_layoutsCarried = false;
    g_quickActions.clear();
    Strings::SetCurrent(Language::English); // absent means the default, like everything here
    TabSettings current;
    QuickAction action;
    std::vector<QuickAction> actions;
    DisplayLayouts::Layout layout;
    std::vector<DisplayLayouts::Layout> layouts;
    bool inTab = false;
    bool inSettings = false;
    bool inAction = false;
    bool inLayout = false;
    bool sawSection = false;
    float fileVersion = 1.0f; // no configVersion key: a file from before there was one

    auto flush = [&] {
        if (inAction) actions.push_back(action);
        if (inLayout) layouts.push_back(layout);
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

        if (line == L"[tab]" || line == L"[settings]" || line == L"[action]" || line == L"[layout]") {
            flush();
            sawSection = true;
            inTab = line == L"[tab]";
            inSettings = line == L"[settings]";
            inAction = line == L"[action]";
            inLayout = line == L"[layout]";
            if (inTab) current = TabSettings{};
            if (inAction) action = QuickAction{};
            if (inLayout) layout = DisplayLayouts::Layout{};
            continue;
        }

        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || (!inTab && !inSettings && !inAction && !inLayout)) continue;
        const std::wstring key = line.substr(0, eq);
        const std::wstring value = line.substr(eq + 1);

        if (inAction) {
            if (key == L"name") action.name = value;
            else if (key == L"target") action.target = value;
            else if (key == L"args") action.arguments = value;
            continue;
        }
        if (inLayout) {
            if (key == L"name") layout.name = value;
            else if (key == L"monitors") layout.ids = DisplayLayouts::SplitIds(value);
            continue;
        }

        if (inSettings) {
            if (key == L"configVersion") ParseFloat(value, fileVersion);
            if (key == L"theme") g_themeMode = ThemeModeFromString(value);
            if (key == L"tvMonitor") g_tvMonitor = Upper(value);
            if (key == L"mainMonitor") g_mainMonitor = Upper(value);
            // Only an explicit "off" turns it off; anything unreadable keeps the safe default.
            if (key == L"fullscreenGuard" && (value == L"0" || value == L"false")) {
                g_fullscreenGuard = false;
            }
            if (key == L"restartAfterCrash" && (value == L"0" || value == L"false")) {
                g_restartAfterCrash = false;
            }
            if (key == L"openDelayMs") {
                float ms = 0.0f;
                if (ParseFloat(value, ms)) SetOpenDelayMs(static_cast<int>(std::lround(ms)));
            }
            if (key == L"startupHintShown" && (value == L"1" || value == L"true")) {
                g_startupHintShown = true;
            }
            if (key == L"recentFolder") g_recentFolder = RecentFiles::SanitizeFolder(value);
            if (key == L"language") Strings::SetCurrent(Strings::FromCode(value));
            if (key == L"displayLayoutsCarried" && (value == L"1" || value == L"true")) g_layoutsCarried = true;
            // Each keeps its default when the value does not parse: a typo must not switch it off.
            if (key == L"shortcutExit") Shortcuts::Parse(value, g_shortcuts.exit);
            if (key == L"shortcutPause") Shortcuts::Parse(value, g_shortcuts.pause);
            if (key == L"shortcutTabs") Shortcuts::ParseModifiers(value, g_shortcuts.tabModifiers);
            if (key == L"shortcutDisplays") Shortcuts::ParseModifiers(value, g_shortcuts.displayModifiers);
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
        } else if (key == L"edge") {
            current.edge = EdgeFromString(value);
        } else if (key == L"monitor") {
            current.monitor = Upper(value);
        }
        // An unknown key is ignored, which is what makes adding one later a
        // compatible change.
    }
    flush();

    // No [action] sections (a file from before the actions were editable) and nothing usable in
    // them both mean the original three, which QuickActionList() supplies for an empty list.
    g_quickActions = QuickActions::Sanitize(std::move(actions));
    g_layouts = DisplayLayouts::Sanitize(std::move(layouts));

    // Text without a single section is not a settings file - zeros left by a power cut, or a file
    // saved over by mistake. A newer EdgeDeck's file is read, but this version would drop what it
    // does not know. Either way the file is kept aside before anything writes over it.
    if (!sawSection && HasContent(contents)) {
        KeepAside(path, LoadProblem::Unreadable);
    } else if (fileVersion > static_cast<float>(kFileVersion)) {
        KeepAside(path, LoadProblem::Newer);
    }

    // An empty but perfectly valid file (the user removed every tab) is honoured
    // as "no tabs"; a file we could not make sense of falls back to defaults.
    return result.empty() ? DefaultTabs() : result;
}

bool Save(const std::vector<TabSettings>& tabs) {
    const std::wstring path = FilePath();
    if (path.empty()) return false;

    const std::wstring temp = path + L".tmp";

    std::wostringstream file;
    file.imbue(std::locale::classic()); // '.' for decimals, whatever the user's region says

    // Globals first, in their own section. Written even when every value is
    // the default, so the file says what it means rather than what was
    // changed.
    file << L"[settings]\n";
    file << L"configVersion=" << kFileVersion << L"\n";
    file << L"theme=" << ThemeModeToString(g_themeMode) << L"\n";
    file << L"language=" << Strings::Code(Strings::Current()) << L"\n";
    file << L"shortcutExit=" << Shortcuts::Format(g_shortcuts.exit) << L"\n";
    file << L"shortcutPause=" << Shortcuts::Format(g_shortcuts.pause) << L"\n";
    file << L"shortcutTabs=" << Shortcuts::FormatModifiers(g_shortcuts.tabModifiers) << L"\n";
    file << L"shortcutDisplays=" << Shortcuts::FormatModifiers(g_shortcuts.displayModifiers) << L"\n";
    if (!g_tvMonitor.empty()) file << L"tvMonitor=" << g_tvMonitor << L"\n";
    if (!g_mainMonitor.empty()) file << L"mainMonitor=" << g_mainMonitor << L"\n";
    file << L"fullscreenGuard=" << (g_fullscreenGuard ? L"1" : L"0") << L"\n";
    file << L"restartAfterCrash=" << (g_restartAfterCrash ? L"1" : L"0") << L"\n";
    file << L"openDelayMs=" << g_openDelayMs << L"\n";
    if (g_startupHintShown) file << L"startupHintShown=1\n";
    if (!g_recentFolder.empty()) file << L"recentFolder=" << g_recentFolder << L"\n";
    if (g_layoutsCarried) file << L"displayLayoutsCarried=1\n";

    for (const auto& t : tabs) {
        file << L"[tab]\n";
        file << L"widget=" << WidgetTypeToString(t.widgetType) << L"\n";
        file << L"verticalRatio=" << t.verticalRatio << L"\n";
        file << L"tabWidth=" << t.tabWidth << L"\n";
        file << L"tabHeight=" << t.tabHeight << L"\n";
        file << L"panelWidth=" << t.panelWidth << L"\n";
        file << L"edge=" << EdgeToString(t.edge) << L"\n";
        if (!t.monitor.empty()) file << L"monitor=" << t.monitor << L"\n";
    }

    // The rows of the Quick Actions panels. Written whenever they are not the defaults, so a
    // file that never had them stays as it was, and "no sections" keeps meaning "the defaults".
    if (!g_quickActions.empty()) {
        for (const QuickAction& a : g_quickActions) {
            file << L"[action]\n";
            file << L"name=" << a.name << L"\n";
            file << L"target=" << a.target << L"\n";
            if (!a.arguments.empty()) file << L"args=" << a.arguments << L"\n";
        }
    }

    for (const DisplayLayouts::Layout& l : g_layouts) {
        file << L"[layout]\n";
        file << L"name=" << l.name << L"\n";
        file << L"monitors=" << DisplayLayouts::JoinIds(l.ids) << L"\n";
    }

    if (!WriteUtf8File(temp, file.str())) {
        DeleteFileW(temp.c_str());
        return false;
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

} // namespace Config

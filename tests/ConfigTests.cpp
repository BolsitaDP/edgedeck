// Covers the config file's parsing and range handling. The loader used to trust
// whatever numbers it found, so a hand-edited or truncated file could ask for a
// negative width or a position off the top of the screen.
#include "TestHarness.h"

#include "Config.h"

#include <string>
#include <vector>

namespace {

std::wstring ConfigDir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";
    return std::wstring(buf) + L"\\EdgeDeck";
}

std::wstring ConfigPath() { return ConfigDir() + L"\\config.txt"; }

// Writes the config file, saving and restoring whatever was there so a test
// run never leaves the user's real configuration changed.
class ScopedConfig {
public:
    ScopedConfig() {
        m_existed = GetFileAttributesW(ConfigPath().c_str()) != INVALID_FILE_ATTRIBUTES;
        if (m_existed) {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (GetFileAttributesExW(ConfigPath().c_str(), GetFileExInfoStandard, &data)) {
                m_size = data.nFileSizeLow;
            }
            HANDLE file = CreateFileW(ConfigPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                m_backup.resize(m_size);
                DWORD read = 0;
                ReadFile(file, m_backup.data(), m_size, &read, nullptr);
                m_backup.resize(read);
                CloseHandle(file);
            }
        }
    }

    ~ScopedConfig() {
        if (m_existed && !m_backup.empty()) {
            HANDLE file = CreateFileW(ConfigPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(file, m_backup.data(), static_cast<DWORD>(m_backup.size()), &written,
                          nullptr);
                CloseHandle(file);
            }
        } else {
            DeleteFileW(ConfigPath().c_str());
        }
    }

    static void Write(const std::wstring& contents) {
        HANDLE file = CreateFileW(ConfigPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, contents.c_str(), -1, nullptr, 0, nullptr,
                                              nullptr);
        std::string utf8(static_cast<size_t>(bytes > 0 ? bytes - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, contents.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    }

private:
    bool m_existed = false;
    DWORD m_size = 0;
    std::string m_backup;
};

void TestRoundTrip() {
    TEST("Config: a saved tab list reads back identically");
    ScopedConfig guard;
    ScopedConfig::Write(L"");

    std::vector<TabSettings> tabs = {
        {WidgetType::QuickActions, 0.10f, 30.0f, 90.0f, 340.0f},
        {WidgetType::Media, 0.50f, 26.0f, 76.0f, 320.0f},
        {WidgetType::Volume, 0.90f, 22.0f, 60.0f, 300.0f},
    };
    CHECK(Config::Save(tabs));

    auto loaded = Config::LoadOrDefault();
    CHECK_EQ(loaded.size(), size_t{3});
    if (loaded.size() == 3) {
        CHECK(loaded[0].widgetType == WidgetType::QuickActions);
        CHECK(loaded[1].widgetType == WidgetType::Media);
        CHECK(loaded[2].widgetType == WidgetType::Volume);
        CHECK_EQ(static_cast<int>(loaded[2].verticalRatio * 100), 90);
        CHECK_EQ(static_cast<int>(loaded[0].tabWidth), 30);
    }
}

void TestLegacyAndUnknownTypes() {
    TEST("Config: legacy and unknown widget names resolve sensibly");
    ScopedConfig guard;
    // "Spotify" is the pre-rename value for the media widget; anything else
    // unrecognised falls back to Quick Actions rather than being dropped.
    ScopedConfig::Write(L"[tab]\nwidget=Spotify\nverticalRatio=0.5\n"
                        L"[tab]\nwidget=Nonsense\nverticalRatio=0.5\n");
    auto loaded = Config::LoadOrDefault();
    CHECK_EQ(loaded.size(), size_t{2});
    if (loaded.size() == 2) {
        CHECK(loaded[0].widgetType == WidgetType::Media);
        CHECK(loaded[1].widgetType == WidgetType::QuickActions);
    }
}

void TestOutOfRangeValuesAreClamped() {
    TEST("Config: out-of-range numbers from the file are clamped, not trusted");
    ScopedConfig guard;
    ScopedConfig::Write(L"[tab]\n"
                        L"widget=Media\n"
                        L"verticalRatio=99\n"
                        L"tabWidth=-500\n"
                        L"tabHeight=100000\n"
                        L"panelWidth=0\n");
    auto loaded = Config::LoadOrDefault();
    CHECK_EQ(loaded.size(), size_t{1});
    if (loaded.size() == 1) {
        const TabSettings& t = loaded[0];
        CHECK(t.verticalRatio >= 0.0f && t.verticalRatio <= 1.0f);
        CHECK(t.tabWidth >= ConfigLimits::kMinTabWidth);
        CHECK(t.tabWidth <= ConfigLimits::kMaxTabWidth);
        CHECK(t.tabHeight <= ConfigLimits::kMaxTabHeight);
        CHECK(t.panelWidth >= ConfigLimits::kMinPanelWidth);
    }
}

void TestMalformedNumbersKeepDefaults() {
    TEST("Config: a non-numeric value leaves the default in place");
    ScopedConfig guard;
    ScopedConfig::Write(L"[tab]\n"
                        L"widget=Media\n"
                        L"verticalRatio=abc\n"
                        L"tabWidth=12abc\n"
                        L"tabHeight=\n"
                        L"panelWidth=1e9999\n");
    auto loaded = Config::LoadOrDefault();
    CHECK_EQ(loaded.size(), size_t{1});
    if (loaded.size() == 1) {
        const TabSettings& t = loaded[0];
        CHECK_EQ(static_cast<int>(t.verticalRatio * 100), 50);
        CHECK_EQ(static_cast<int>(t.tabWidth), 26);
        CHECK_EQ(static_cast<int>(t.tabHeight), 76);
        CHECK_EQ(static_cast<int>(t.panelWidth), 300);
    }
}

void TestTruncatedFileFallsBack() {
    TEST("Config: a file with no complete tab block yields the defaults");
    ScopedConfig guard;
    ScopedConfig::Write(L"[tab]\nwidget=M"); // ends mid-value, as a crash would leave it
    auto loaded = Config::LoadOrDefault();
    CHECK(!loaded.empty());
}

void TestUnknownKeysIgnored() {
    TEST("Config: unknown keys are ignored, keeping the change forward-compatible");
    ScopedConfig guard;
    ScopedConfig::Write(L"[tab]\nwidget=Media\nfutureOption=42\nverticalRatio=0.25\n");
    auto loaded = Config::LoadOrDefault();
    CHECK_EQ(loaded.size(), size_t{1});
    if (loaded.size() == 1) CHECK_EQ(static_cast<int>(loaded[0].verticalRatio * 100), 25);
}

void TestEmptyFileUsesDefaults() {
    TEST("Config: an unreadable/empty file falls back to the default tabs");
    ScopedConfig guard;
    DeleteFileW(ConfigPath().c_str());
    CHECK(!Config::LoadOrDefault().empty());
}

void TestThemeModeRoundTrips() {
    TEST("Config: the theme preference survives a save and load");
    ScopedConfig guard;
    for (ThemeMode mode : {ThemeMode::Follow, ThemeMode::Dark, ThemeMode::Light}) {
        // Load before setting, not after: LoadOrDefault reads the [settings] section
        // as a side effect and would overwrite a mode just set in memory. The real
        // flow does the same - load once at startup, then the dialog sets and saves.
        const std::vector<TabSettings> tabs = Config::LoadOrDefault();
        Config::SetCurrentThemeMode(mode);
        CHECK(Config::Save(tabs));
        Config::SetCurrentThemeMode(ThemeMode::Follow);
        Config::LoadOrDefault();
        CHECK_EQ(static_cast<int>(Config::CurrentThemeMode()), static_cast<int>(mode));
    }
}

void TestUnknownThemeFallsBackToFollow() {
    TEST("Config: an unrecognised theme value falls back to following Windows");
    ScopedConfig guard;
    ScopedConfig::Write(L"[settings]\ntheme=Ultraviolet\n[tab]\nwidget=Media\n");
    Config::SetCurrentThemeMode(ThemeMode::Dark);
    Config::LoadOrDefault();
    CHECK_EQ(static_cast<int>(Config::CurrentThemeMode()),
             static_cast<int>(ThemeMode::Follow));
}

void TestSettingsSectionDoesNotDisturbTabs() {
    TEST("Config: the [settings] section is not mistaken for a tab");
    ScopedConfig guard;
    ScopedConfig::Write(L"[settings]\ntheme=Dark\n[tab]\nwidget=Volume\nverticalRatio=0.5\n"
                       L"[tab]\nwidget=Brightness\nverticalRatio=0.75\n");
    Config::SetCurrentThemeMode(ThemeMode::Follow);
    auto loaded = Config::LoadOrDefault();
    CHECK_EQ(loaded.size(), size_t{2});
    CHECK_EQ(static_cast<int>(Config::CurrentThemeMode()), static_cast<int>(ThemeMode::Dark));
    if (loaded.size() == 2) {
        CHECK_EQ(static_cast<int>(loaded[0].widgetType), static_cast<int>(WidgetType::Volume));
        CHECK_EQ(static_cast<int>(loaded[1].widgetType), static_cast<int>(WidgetType::Brightness));
    }
}

} // namespace

void TestDisplaysWidgetAndTvMonitorRoundTrip() {
    TEST("Config: the Displays tab and the TV override survive a save and reload");
    ScopedConfig guard;
    ScopedConfig::Write(L"");

    Config::SetDisplayTvMonitor(L"sam7a08"); // typed in lower case on purpose
    std::vector<TabSettings> tabs = {{WidgetType::Displays, 0.5f, 26.0f, 76.0f, 300.0f}};
    CHECK(Config::Save(tabs));

    Config::SetDisplayTvMonitor(L"");
    auto loaded = Config::LoadOrDefault();
    CHECK_EQ(loaded.size(), size_t{1});
    if (loaded.size() == 1) CHECK(loaded[0].widgetType == WidgetType::Displays);
    CHECK_EQ(Config::DisplayTvMonitor(), std::wstring(L"SAM7A08"));
}

void TestTvMonitorDefaultsToEmpty() {
    TEST("Config: with no tvMonitor line the TV is left to be detected");
    ScopedConfig guard;
    Config::SetDisplayTvMonitor(L"STALE");
    ScopedConfig::Write(L"[settings]\ntheme=Dark\n[tab]\nwidget=Displays\n");
    Config::LoadOrDefault();
    CHECK_EQ(Config::DisplayTvMonitor(), std::wstring());
}

void TestFullscreenGuardRoundTrip() {
    TEST("Config: the full-screen guard defaults on, and only an explicit 'off' turns it off");
    ScopedConfig guard;

    ScopedConfig::Write(L"[tab]\nwidget=Media\n");
    Config::LoadOrDefault();
    CHECK(Config::FullscreenGuard()); // absent: the safe default

    ScopedConfig::Write(L"[settings]\nfullscreenGuard=0\n[tab]\nwidget=Media\n");
    Config::LoadOrDefault();
    CHECK(!Config::FullscreenGuard());

    ScopedConfig::Write(L"[settings]\nfullscreenGuard=false\n[tab]\nwidget=Media\n");
    Config::LoadOrDefault();
    CHECK(!Config::FullscreenGuard());

    // Anything unreadable keeps the guard on rather than silently disabling it.
    ScopedConfig::Write(L"[settings]\nfullscreenGuard=maybe\n[tab]\nwidget=Media\n");
    Config::LoadOrDefault();
    CHECK(Config::FullscreenGuard());

    // A save and reload keeps it, either way round.
    std::vector<TabSettings> tabs = {{WidgetType::Media, 0.5f, 26.0f, 76.0f, 320.0f}};
    Config::SetFullscreenGuard(false);
    CHECK(Config::Save(tabs));
    Config::SetFullscreenGuard(true);
    Config::LoadOrDefault();
    CHECK(!Config::FullscreenGuard());

    Config::SetFullscreenGuard(true);
    CHECK(Config::Save(tabs));
    Config::SetFullscreenGuard(false);
    Config::LoadOrDefault();
    CHECK(Config::FullscreenGuard());
}

void TestRestartAfterCrashRoundTrip() {
    TEST("Config: restarting after a crash defaults on, and only an explicit 'off' turns it off");
    ScopedConfig guard;

    ScopedConfig::Write(L"[tab]\nwidget=Media\n");
    Config::LoadOrDefault();
    CHECK(Config::RestartAfterCrash());

    ScopedConfig::Write(L"[settings]\nrestartAfterCrash=0\n[tab]\nwidget=Media\n");
    Config::LoadOrDefault();
    CHECK(!Config::RestartAfterCrash());

    ScopedConfig::Write(L"[settings]\nrestartAfterCrash=sometimes\n[tab]\nwidget=Media\n");
    Config::LoadOrDefault();
    CHECK(Config::RestartAfterCrash()); // unreadable: keep the default

    std::vector<TabSettings> tabs = {{WidgetType::Media, 0.5f, 26.0f, 76.0f, 320.0f}};
    Config::SetRestartAfterCrash(false);
    CHECK(Config::Save(tabs));
    Config::SetRestartAfterCrash(true);
    Config::LoadOrDefault();
    CHECK(!Config::RestartAfterCrash());
    Config::SetRestartAfterCrash(true);
}

void RunConfigTests() {
    TestRoundTrip();
    TestFullscreenGuardRoundTrip();
    TestRestartAfterCrashRoundTrip();
    TestDisplaysWidgetAndTvMonitorRoundTrip();
    TestTvMonitorDefaultsToEmpty();
    TestLegacyAndUnknownTypes();
    TestOutOfRangeValuesAreClamped();
    TestMalformedNumbersKeepDefaults();
    TestTruncatedFileFallsBack();
    TestUnknownKeysIgnored();
    TestEmptyFileUsesDefaults();
    TestThemeModeRoundTrips();
    TestUnknownThemeFallsBackToFollow();
    TestSettingsSectionDoesNotDisturbTabs();
}

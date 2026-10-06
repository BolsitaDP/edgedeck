#pragma once

#include <windows.h>

#include <vector>

#include "PanelWidget.h"

// The user-configurable subset of a tab's layout. Tab's own TabConfig also
// carries fixed constants (corner radius, animation timing) that aren't
// exposed in the settings UI; App maps between the two.
struct TabSettings {
    WidgetType widgetType = WidgetType::QuickActions;
    float verticalRatio = 0.5f;
    float tabWidth = 26.0f;
    float tabHeight = 76.0f;
    float panelWidth = 300.0f;
};

// Range limits, in one place so the settings UI, the loader and Tab all agree.
// A hand-edited or truncated config file goes through exactly the same
// clamping as the UI, so it can no longer ask for a negative width or a
// position off the top of the screen.
namespace ConfigLimits {
constexpr float kMinTabWidth = 12.0f;
constexpr float kMaxTabWidth = 60.0f;
constexpr float kMinTabHeight = 40.0f;
constexpr float kMaxTabHeight = 200.0f;
constexpr float kMinPanelWidth = 180.0f;
constexpr float kMaxPanelWidth = 520.0f;

inline void Clamp(TabSettings& s) {
    if (!(s.verticalRatio >= 0.0f)) s.verticalRatio = 0.5f; // also catches NaN
    if (s.verticalRatio > 1.0f) s.verticalRatio = 1.0f;
    if (!(s.tabWidth >= kMinTabWidth)) s.tabWidth = 26.0f;
    if (s.tabWidth > kMaxTabWidth) s.tabWidth = kMaxTabWidth;
    if (!(s.tabHeight >= kMinTabHeight)) s.tabHeight = 76.0f;
    if (s.tabHeight > kMaxTabHeight) s.tabHeight = kMaxTabHeight;
    if (!(s.panelWidth >= kMinPanelWidth)) s.panelWidth = 300.0f;
    if (s.panelWidth > kMaxPanelWidth) s.panelWidth = kMaxPanelWidth;
}
} // namespace ConfigLimits

// Which palette the panel uses. Follow is the default and the reason the panel
// normally looks like part of Windows; the other two exist because a light
// system with a preferred dark panel (or the reverse) is not an unusual want.
enum class ThemeMode : int {
    Follow = 0,
    Dark = 1,
    Light = 2,
};

// Tiny hand-rolled key=value file under %LOCALAPPDATA%\EdgeDeck\ - no JSON
// library, written only when the user hits Save in the settings window.
namespace Config {

// The enum order matches the combo box order in the dialog, so the selected
// index and the stored value are the same number.
const wchar_t* ThemeModeToString(ThemeMode mode);
ThemeMode ThemeModeFromString(const std::wstring& value);

// Returns the saved tabs, or a default set if there is no readable file.
// Every entry is clamped through ConfigLimits::Clamp before it is returned.
// Also picks up the global [settings] section as a side effect.
std::vector<TabSettings> LoadOrDefault();

// The current theme preference, and the setter the dialog uses. Held in memory
// and persisted by the next Save, which writes it alongside the tabs rather
// than as a separate file write.
ThemeMode CurrentThemeMode();
void SetCurrentThemeMode(ThemeMode mode);

// Which monitor the Displays tab treats as the TV, as its EDID id (the vendor +
// product code, e.g. "SAM7A08"). Empty - the default - means "the largest panel",
// which is right for a 4K TV beside desktop monitors; set it by hand in
// config.txt ([settings] tvMonitor=...) when that guess is wrong. Held in memory
// like the theme and written back by Save so a Settings round trip keeps it.
std::wstring DisplayTvMonitor();
void SetDisplayTvMonitor(const std::wstring& id);

// Written to a sibling temp file and swapped in atomically, so a crash or a
// full disk can never leave a half-written file that later loads as "one tab
// instead of three". Returns false if the file could not be committed.
bool Save(const std::vector<TabSettings>& tabs);

// Absolute path of the config file, or empty if %LOCALAPPDATA% is unusable.
std::wstring FilePath();

} // namespace Config

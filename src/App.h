#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "Config.h"

class Tab;
class SettingsWindow;

// Shared window class names, registered once by App::Create and referenced
// by Tab when it creates its own HWNDs.
inline constexpr wchar_t kTabClassName[] = L"EdgeDeckTabWindow";
inline constexpr wchar_t kPanelClassName[] = L"EdgeDeckPanelWindow";

// Resource ids from res/EdgeDeck.rc.
inline constexpr int kIconIdEdgeDeck = 101;

// Top-level orchestrator: owns every Tab, the shared window classes, the
// process-wide hotkeys, the notification-area icon that is the safety net for
// reaching Settings/Exit, and the settings window.
// Legitimately a singleton (one per process), unlike Tab which is not.
class App {
public:
    // Declared out-of-line (defined where Tab's complete type is visible) -
    // std::vector<std::unique_ptr<Tab>> needs Tab complete to generate the
    // implicit constructor/destructor's member cleanup, which main.cpp
    // (only forward-declaring Tab) can't provide.
    App();
    ~App();

    bool Create(HINSTANCE hInstance);
    static int RunMessageLoop();

    void ShowTabContextMenu(Tab* tab, POINT screenPt);
    void RequestExit();
    void OpenSettings();

    // A message for the user that does not need an answer, shown by the
    // notification-area icon. The non-modal way to say "that failed": a
    // MessageBox from a background reply froze the user's work, and one raised
    // before the message loop started held the whole app up behind a dialog
    // nobody could see.
    void ShowBalloon(const wchar_t* text);

    // Opens and pins the nth tab (0-based). Wired to Ctrl+Alt+1..9, which is
    // the keyboard route into every panel - the panels themselves never take
    // focus, so without this they would be mouse-only.
    void OpenTabByIndex(size_t index);

private:
    bool CreateUtilityWindow(HINSTANCE hInstance);
    bool AddTrayIcon();
    void RemoveTrayIcon();
    void ApplySettings(const std::vector<TabSettings>& settings);
    bool BuildTabsFrom(const std::vector<TabSettings>& settings, HINSTANCE hInstance);

    // Registers the display-layout shortcuts if a Displays tab exists and drops
    // them if it does not, so they track the tab list through Settings changes.
    void UpdateDisplayHotkeys();
    // Passes a layout shortcut to the first Displays tab.
    void SwitchDisplays(int profileIndex);

    static LRESULT CALLBACK UtilityProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_utilityHwnd = nullptr;
    std::vector<std::unique_ptr<Tab>> m_tabs;
    std::unique_ptr<SettingsWindow> m_settingsWindow;
    bool m_trayAdded = false;

    // A problem found before the tray icon exists (the icon is added last, once the
    // tabs are up), held until it can be shown.
    std::wstring m_startupNotice;

    static constexpr int kExitHotkeyId = 1;
    // Ctrl+Alt+1..9 open the corresponding tab. Ids 2..10 so they cannot clash
    // with the exit hotkey's id.
    static constexpr int kOpenHotkeyBase = 2;
    static constexpr int kOpenHotkeyCount = 9;
    // Ctrl+Alt+Shift+1..3: the Displays tab's three layouts, in the order its
    // buttons appear. The extra Shift keeps them clear of the tab openers, and
    // they are registered only while there is a Displays tab to act on, so an
    // install that never uses the widget never claims the keys.
    static constexpr int kDisplayHotkeyBase = kOpenHotkeyBase + kOpenHotkeyCount;
    static constexpr int kDisplayHotkeyCount = 3;

    // Notification-area icon callback. Outside the widget range on purpose:
    // App's own private message, not a widget's.
    static constexpr UINT kTrayCallbackMessage = WM_APP + 190;

    static App* s_instance;
};

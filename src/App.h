#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "Config.h"
#include "QuickAction.h"
#include "Strings.h"

class Tab;
class SettingsWindow;
class ActionsWindow;
class ShortcutsWindow;

// Shared window class names, registered once by App::Create and referenced
// by Tab when it creates its own HWNDs.
inline constexpr wchar_t kTabClassName[] = L"EdgeDeckTabWindow";
inline constexpr wchar_t kPanelClassName[] = L"EdgeDeckPanelWindow";

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

    // Call before Create when this copy was started because the previous one crashed: the
    // user is told once, from the tray, instead of finding out from a log.
    void NoteRestartedAfterCrash();

    bool Create(HINSTANCE hInstance);
    static int RunMessageLoop();

    void ShowTabContextMenu(Tab* tab, POINT screenPt);
    void RequestExit();
    void OpenSettings();
    // The window where the Quick Actions list is edited. Opened from Settings, from the tray
    // menu and from the menu of any tab.
    void OpenActionsEditor();

    // Pausing hides every tab (and closes any panel) until resumed: for a game or a presentation,
    // where a pointer pushed against the screen edge should meet nothing. The keyboard shortcuts
    // keep working. Not kept across runs.
    void TogglePaused();
    bool IsPaused() const { return m_paused; }
    // Starts a new copy with the same arguments and exits; the new one waits for this process to
    // be gone. The menu's "Restart EdgeDeck".
    void Restart();

    // A message for the user that does not need an answer, shown by the
    // notification-area icon. The non-modal way to say "that failed": a
    // MessageBox from a background reply froze the user's work, and one raised
    // before the message loop started held the whole app up behind a dialog
    // nobody could see.
    // warning picks the icon the notification carries: a fault, or just information.
    void ShowBalloon(const wchar_t* text, bool warning = true);

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

    // Looks at the Start-with-Windows entry at startup: repairs one whose file is gone, and
    // queues a note when something is worth saying (see Autostart::NoteForStartup).
    void CheckAutostart();

    // The menu of the notification-area icon and of a tab's right click: one list, so the two
    // cannot drift apart. Runs the chosen command.
    void ShowMenu(POINT screenPt, HWND owner);
    void RunMenuCommand(UINT command);
    void ToggleAutostart();
    void OpenLog();
    // The Quick Actions list was edited: keep it, and lay the panels that show it out again.
    void ApplyActions(std::vector<QuickAction> actions);
    // The notification-area tooltip, which says when the tabs are paused.
    void UpdateTrayTip();

    // Registers the display-layout shortcuts if a Displays tab exists and drops
    // them if it does not, so they track the tab list through Settings changes.
    void UpdateDisplayHotkeys(std::vector<std::wstring>* taken = nullptr);
    // Registers the shortcuts the settings name (and drops the old ones); returns, as shown to a
    // person, those another program already owns.
    std::vector<std::wstring> RegisterShortcuts();
    // The Shortcuts window: keeps and registers the set, and returns what could not be registered
    // as a sentence (empty when all of it was).
    void OpenShortcutsEditor();
    std::wstring ApplyShortcuts(const Shortcuts::Set& set);
    // Passes a layout shortcut to the first Displays tab.
    void SwitchDisplays(int profileIndex);

    static LRESULT CALLBACK UtilityProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_utilityHwnd = nullptr;
    std::vector<std::unique_ptr<Tab>> m_tabs;
    std::unique_ptr<SettingsWindow> m_settingsWindow;
    std::unique_ptr<ActionsWindow> m_actionsWindow;
    std::unique_ptr<ShortcutsWindow> m_shortcutsWindow;
    // What the tabs were last built from. Kept so a change to one global (the startup hint, the
    // Quick Actions list) can be saved without reading the file back and losing a change that
    // is only in memory.
    std::vector<TabSettings> m_settings;
    bool m_paused = false;
    bool m_trayAdded = false;

    // A problem found before the tray icon exists (the icon is added last, once the
    // tabs are up), held until it can be shown.
    std::wstring m_startupNotice;
    void AddStartupNotice(const std::wstring& text);
    // Found before the settings (and the language) are read, and worded after.
    bool m_restartedAfterCrash = false;
    // The exit shortcut is owned by another program (the tray menu still has Exit).
    bool m_exitShortcutTaken = false;
    // The language the panels were last drawn in, to know when a Save changed it.
    Language m_language = Language::English;
    // "EdgeDeck 1.2.0", or with "(paused)" in the current language.
    std::wstring TrayTip() const;

    static constexpr int kExitHotkeyId = 1;
    // Ctrl+Alt+1..9 open the corresponding tab. Ids 2..10 so they cannot clash
    // with the exit hotkey's id.
    static constexpr int kOpenHotkeyBase = 2;
    static constexpr int kOpenHotkeyCount = 9;
    // Ctrl+Alt+Shift+1..4: the Displays tab's four layouts, in the order its
    // buttons appear. The extra Shift keeps them clear of the tab openers, and
    // they are registered only while there is a Displays tab to act on, so an
    // install that never uses the widget never claims the keys.
    static constexpr int kDisplayHotkeyBase = kOpenHotkeyBase + kOpenHotkeyCount;
    static constexpr int kDisplayHotkeyCount = 4;
    // Ctrl+Alt+P: pause or resume the tabs.
    static constexpr int kPauseHotkeyId = kDisplayHotkeyBase + kDisplayHotkeyCount;

    // Notification-area icon callback. Outside the widget range on purpose:
    // App's own private message, not a widget's.
    static constexpr UINT kTrayCallbackMessage = WM_APP + 190;

    static App* s_instance;
};

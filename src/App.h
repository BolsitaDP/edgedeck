#pragma once

#include <windows.h>
#include <memory>
#include <vector>
#include "Config.h"

class Tab;
class SettingsWindow;

// Shared window class names, registered once by App::Create and referenced
// by Tab when it creates its own HWNDs.
inline constexpr wchar_t kTabClassName[] = L"EdgeDeckTabWindow";
inline constexpr wchar_t kPanelClassName[] = L"EdgeDeckPanelWindow";

// Top-level orchestrator: owns every Tab, the shared window classes, the
// process-wide exit hotkey and the settings window.
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

private:
    bool CreateUtilityWindow(HINSTANCE hInstance);
    void ApplySettings(const std::vector<TabSettings>& settings);
    bool BuildTabsFrom(const std::vector<TabSettings>& settings, HINSTANCE hInstance);

    static LRESULT CALLBACK UtilityProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance = nullptr;
    HWND m_utilityHwnd = nullptr;
    std::vector<std::unique_ptr<Tab>> m_tabs;
    std::unique_ptr<SettingsWindow> m_settingsWindow;

    static constexpr int kExitHotkeyId = 1;

    static App* s_instance;
};

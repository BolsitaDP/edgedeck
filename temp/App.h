#pragma once
#include <windows.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
class Tab;
struct TabSettings;
class SettingsWindow;
inline constexpr wchar_t kTabClassName[] = L"EdgeDeckTabWindow";
inline constexpr wchar_t kPanelClassName[] = L"EdgeDeckPanelWindow";
class App {
public:
    App();
    ~App();
    bool Create(HINSTANCE hInstance);
    static int RunMessageLoop();
    void Quit();
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
};
extern App* g_pAppInstance;
#endif // APP_H

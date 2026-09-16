#include "App.h"
#include "Tab.h"
#include "QuickActionsWidget.h"
#include "MediaWidget.h"
#include "SettingsWindow.h"

#include <algorithm>

namespace {
const wchar_t kUtilityClassName[] = L"EdgeDeckUtilityWindow";
constexpr UINT kMenuIdExit = 100;
constexpr UINT kMenuIdSettings = 101;

std::unique_ptr<PanelWidget> MakeWidget(WidgetType type) {
    if (type == WidgetType::Media) return std::make_unique<MediaWidget>();
    return std::make_unique<QuickActionsWidget>();
}
} // namespace

App* App::s_instance = nullptr;

App::App() = default;

App::~App() {
    if (m_mouseHook) {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
    m_tabs.clear();
    if (m_utilityHwnd && IsWindow(m_utilityHwnd)) {
        DestroyWindow(m_utilityHwnd);
    }
    s_instance = nullptr;
}

bool App::Create(HINSTANCE hInstance) {
    s_instance = this;
    m_hInstance = hInstance;

    if (!Tab::RegisterClasses(hInstance)) return false;
    if (!CreateUtilityWindow(hInstance)) return false;

    return BuildTabsFrom(Config::LoadOrDefault(), hInstance);
}

bool App::BuildTabsFrom(const std::vector<TabSettings>& settings, HINSTANCE hInstance) {
    std::vector<std::unique_ptr<Tab>> newTabs;
    for (const auto& s : settings) {
        TabConfig config;
        config.verticalRatio = s.verticalRatio;
        config.tabWidth = s.tabWidth;
        config.tabHeight = s.tabHeight;
        config.panelWidth = s.panelWidth;

        auto tab = std::make_unique<Tab>(this, config, MakeWidget(s.widgetType));
        if (!tab->Create(hInstance)) return false;
        newTabs.push_back(std::move(tab));
    }
    m_tabs = std::move(newTabs); // old tabs (if any) are destroyed here
    UpdateMouseHook();           // nothing can still be pinned from the old set
    return true;
}

void App::OpenSettings() {
    if (m_settingsWindow && IsWindow(m_settingsWindow->Hwnd())) {
        SetForegroundWindow(m_settingsWindow->Hwnd());
        return;
    }

    m_settingsWindow = std::make_unique<SettingsWindow>();
    m_settingsWindow->Create(m_hInstance, Config::LoadOrDefault(),
                              [this](const std::vector<TabSettings>& settings) {
                                  ApplySettings(settings);
                              });
}

void App::ApplySettings(const std::vector<TabSettings>& settings) {
    Config::Save(settings);
    BuildTabsFrom(settings, m_hInstance);
}

bool App::CreateUtilityWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = hInstance;
    wc.lpszClassName = kUtilityClassName;
    wc.lpfnWndProc = UtilityProc;
    if (!RegisterClassExW(&wc)) return false;

    // Never shown - exists purely to host the exit hotkey and the tab
    // context menu (TrackPopupMenu needs some owner HWND for the standard
    // NOACTIVATE-safe dismiss trick; a hidden window works fine for that,
    // same technique tray-icon apps use).
    m_utilityHwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kUtilityClassName, L"EdgeDeck", WS_POPUP, 0,
                                     0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!m_utilityHwnd) return false;

    if (!RegisterHotKey(m_utilityHwnd, kExitHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_ALT, 'Q')) {
        MessageBoxW(nullptr, L"EdgeDeck's exit shortcut (Ctrl+Shift+Alt+Q) is unavailable.",
                    L"EdgeDeck", MB_ICONWARNING | MB_OK | MB_TOPMOST);
    }
    return true;
}

int App::RunMessageLoop() {
    MSG msg{};
    BOOL result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (result == -1) return 1;

        // The settings window is a plain (non-dialog-resource) window built
        // from comctl32 children; IsDialogMessage is what gives it free
        // Tab/arrow/Enter navigation between them without hand-rolled code.
        HWND settingsHwnd =
            (s_instance && s_instance->m_settingsWindow) ? s_instance->m_settingsWindow->Hwnd() : nullptr;
        if (settingsHwnd && IsWindow(settingsHwnd) && IsDialogMessage(settingsHwnd, &msg)) {
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::NotifyPinChanged() { UpdateMouseHook(); }

void App::UpdateMouseHook() {
    bool anyPinned = std::any_of(m_tabs.begin(), m_tabs.end(),
                                  [](const std::unique_ptr<Tab>& t) { return t->IsPinned(); });

    if (anyPinned && !m_mouseHook) {
        m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, m_hInstance, 0);
    } else if (!anyPinned && m_mouseHook) {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
}

void App::HandleGlobalClick(POINT screenPt) {
    for (auto& tab : m_tabs) {
        if (tab->IsPinned() && !tab->IsPointInside(screenPt)) {
            tab->ClosePinned();
        }
    }
}

void App::ShowTabContextMenu(Tab* /*tab*/, POINT screenPt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuIdSettings, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuIdExit, L"Exit");

    // Standard trick for a NOACTIVATE-owning window's popup menu: briefly
    // take the foreground so the menu dismisses correctly on an outside
    // click, then post WM_NULL per MSDN so TrackPopupMenu's state settles.
    SetForegroundWindow(m_utilityHwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, m_utilityHwnd, nullptr);
    PostMessageW(m_utilityHwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

void App::RequestExit() {
    if (m_utilityHwnd) DestroyWindow(m_utilityHwnd);
}

// ---------------------------------------------------------------------------
// Static callbacks. App is a legitimate process-wide singleton (exactly one
// per process), unlike Tab, so a plain static back-pointer is fine here.
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::UtilityProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            if (wParam == kExitHotkeyId && s_instance) {
                s_instance->RequestExit();
            }
            return 0;
        case WM_COMMAND:
            if (!s_instance) return 0;
            if (LOWORD(wParam) == kMenuIdExit) {
                s_instance->RequestExit();
            } else if (LOWORD(wParam) == kMenuIdSettings) {
                s_instance->OpenSettings();
            }
            return 0;
        case WM_DESTROY:
            if (s_instance) UnregisterHotKey(hwnd, kExitHotkeyId);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK App::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && s_instance &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN)) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        s_instance->HandleGlobalClick(info->pt);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

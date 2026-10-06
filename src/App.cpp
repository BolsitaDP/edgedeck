#include "App.h"
#include "Diagnostics.h"
#include "Tab.h"
#include "QuickActionsWidget.h"
#include "MediaWidget.h"
#include "BrightnessWidget.h"
#include "LyricsWidget.h"
#include "VolumeWidget.h"
#include "DisplayWidget.h"
#include "SettingsWindow.h"

#include <shellapi.h>

#include <algorithm>

namespace {
const wchar_t kUtilityClassName[] = L"EdgeDeckUtilityWindow";
constexpr UINT kMenuIdExit = 100;
constexpr UINT kMenuIdSettings = 101;
constexpr UINT kTrayId = 102;

// Ctrl+Alt+Q: the same gesture as the per-tab openers, so it cannot collide
// with them, and a chord nothing else claims.
constexpr int kExitHotkeyVk = 'Q';

// Broadcast by the shell after it (re)builds the notification area, which it
// does whenever Explorer restarts. Every icon is destroyed at that point and the
// owning process has to add its own again - there is no way to opt out, and no
// notification that it happened.
//
// The id is assigned by the shell at runtime, so it cannot be a switch case and
// has to be compared by hand. Registering it twice returns the same value, so
// there is nothing to guard.
UINT TaskbarCreatedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
}

// The user-configurable subset of a tab's layout, mapped onto Tab's full
// geometry. Everything TabConfig carries that is not user-facing (corner
// radius, animation and close timing) keeps its default.
TabConfig MakeTabConfig(const TabSettings& s) {
    TabConfig config;
    config.verticalRatio = s.verticalRatio;
    config.tabWidth = s.tabWidth;
    config.tabHeight = s.tabHeight;
    config.panelWidth = s.panelWidth;
    return config;
}

std::unique_ptr<PanelWidget> MakeWidget(WidgetType type) {
    switch (type) {
        case WidgetType::Media: return std::make_unique<MediaWidget>();
        case WidgetType::Brightness: return std::make_unique<BrightnessWidget>();
        case WidgetType::Lyrics: return std::make_unique<LyricsWidget>();
        case WidgetType::Volume: return std::make_unique<VolumeWidget>();
        case WidgetType::Displays: return std::make_unique<DisplayWidget>();
        default: return std::make_unique<QuickActionsWidget>();
    }
}
} // namespace

App* App::s_instance = nullptr;

App::App() = default;

App::~App() {
    RemoveTrayIcon();
    m_tabs.clear();
    if (m_utilityHwnd && IsWindow(m_utilityHwnd)) {
        DestroyWindow(m_utilityHwnd);
    }
    Tab::UnregisterClasses();
    s_instance = nullptr;
}

bool App::Create(HINSTANCE hInstance) {
    s_instance = this;
    m_hInstance = hInstance;

    if (!Tab::RegisterClasses(hInstance)) return false;
    if (!CreateUtilityWindow(hInstance)) return false;

    if (!BuildTabsFrom(Config::LoadOrDefault(), hInstance)) return false;
    UpdateDisplayHotkeys();
    AddTrayIcon();
    return true;
}

void App::UpdateDisplayHotkeys() {
    static_assert(kDisplayHotkeyCount == DisplayWidget::kProfileCount,
                  "one shortcut per Displays layout");
    if (!m_utilityHwnd) return;

    // Unregister first, so this is the same call whether the tab is new, gone, or
    // was there all along; unregistering a key that was never registered is a no-op.
    for (int i = 0; i < kDisplayHotkeyCount; ++i) {
        UnregisterHotKey(m_utilityHwnd, kDisplayHotkeyBase + i);
    }

    const bool hasDisplaysTab = std::any_of(m_tabs.begin(), m_tabs.end(), [](const auto& tab) {
        return tab->WidgetType() == WidgetType::Displays;
    });
    if (!hasDisplaysTab) return;

    for (int i = 0; i < kDisplayHotkeyCount; ++i) {
        if (!RegisterHotKey(m_utilityHwnd, kDisplayHotkeyBase + i, MOD_CONTROL | MOD_ALT | MOD_SHIFT,
                            '1' + i)) {
            // Logged, not shown: another program owning a chord is not worth a
            // dialog, and the tab's buttons still work.
            Diagnostics::Error("Hotkey: Ctrl+Alt+Shift+%d is already taken by another program",
                               i + 1);
        }
    }
}

void App::SwitchDisplays(int profileIndex) {
    for (auto& tab : m_tabs) {
        if (tab->WidgetType() == WidgetType::Displays) {
            tab->ActivateShortcut(profileIndex);
            return;
        }
    }
}

bool App::BuildTabsFrom(const std::vector<TabSettings>& settings, HINSTANCE hInstance) {
    std::vector<std::unique_ptr<Tab>> newTabs;
    newTabs.reserve(settings.size());
    for (const auto& s : settings) {
        auto tab = std::make_unique<Tab>(this, MakeTabConfig(s), MakeWidget(s.widgetType));
        if (!tab->Create(hInstance)) return false;
        newTabs.push_back(std::move(tab));
    }
    m_tabs = std::move(newTabs); // old tabs (if any) are destroyed here
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
    if (!Config::Save(settings)) {
        // Saving is the one operation whose failure the user would otherwise
        // never find out about: the tabs would move and then quietly revert on
        // the next launch.
        MessageBoxW(nullptr,
                    L"EdgeDeck could not write its settings file.\n\n"
                    L"Your changes will apply for this session but will not be kept.",
                    L"EdgeDeck", MB_ICONWARNING | MB_OK | MB_TOPMOST);
    }

    // The theme may have changed in the same Save. The palette is cached and only
    // re-read on demand, so without this the panels would keep painting in the
    // old colours until something else happened to invalidate it.
    PanelTheme::Refresh();

    // A tab only has to be rebuilt when its widget type changed. Everything
    // else - size, position, order of the untouched tabs - is applied in place,
    // so an open or pinned panel, its scroll position and any in-flight async
    // work all survive the edit. The previous code destroyed and recreated every
    // tab on each Save, which closed every panel and dropped every pin.
    if (m_tabs.size() == settings.size()) {
        bool typesMatch = true;
        for (size_t i = 0; i < settings.size(); ++i) {
            if (m_tabs[i]->WidgetType() != settings[i].widgetType) {
                typesMatch = false;
                break;
            }
        }
        if (typesMatch) {
            for (size_t i = 0; i < settings.size(); ++i) {
                m_tabs[i]->ApplyConfig(MakeTabConfig(settings[i]));
            }
            return;
        }
    }

    if (!BuildTabsFrom(settings, m_hInstance)) {
        MessageBoxW(nullptr, L"EdgeDeck could not rebuild its panels. The previous layout is still "
                            L"in place.",
                    L"EdgeDeck", MB_ICONERROR | MB_OK | MB_TOPMOST);
        return;
    }
    // A Displays tab may have just been added or removed.
    UpdateDisplayHotkeys();
}

void App::OpenTabByIndex(size_t index) {
    if (index >= m_tabs.size()) return;
    m_tabs[index]->RequestOpen();
}

bool App::CreateUtilityWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = hInstance;
    wc.lpszClassName = kUtilityClassName;
    wc.lpfnWndProc = UtilityProc;
    if (!RegisterClassExW(&wc)) return false;

    // Never shown - exists purely to host the exit hotkey, the per-tab openers,
    // the notification-area icon and the tab context menu (TrackPopupMenu needs
    // some owner HWND for the standard NOACTIVATE-safe dismiss trick; a hidden
    // window works fine for that, same technique tray-icon apps use).
    m_utilityHwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kUtilityClassName, L"EdgeDeck", WS_POPUP, 0,
                                     0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!m_utilityHwnd) return false;

    if (!RegisterHotKey(m_utilityHwnd, kExitHotkeyId, MOD_CONTROL | MOD_ALT, kExitHotkeyVk)) {
        MessageBoxW(nullptr, L"EdgeDeck's exit shortcut (Ctrl+Alt+Q) is unavailable.",
                    L"EdgeDeck", MB_ICONWARNING | MB_OK | MB_TOPMOST);
    }

    for (int i = 0; i < kOpenHotkeyCount; ++i) {
        RegisterHotKey(m_utilityHwnd, kOpenHotkeyBase + i, MOD_CONTROL | MOD_ALT, '1' + i);
    }
    return true;
}

bool App::AddTrayIcon() {
    if (!m_utilityHwnd) return false;

    const HINSTANCE instance = m_hInstance;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_utilityHwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayCallbackMessage;
    // The app's own icon from the resource script, so the notification area
    // shows EdgeDeck rather than the generic application glyph.
    nid.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kIconIdEdgeDeck));
    if (!nid.hIcon) {
        nid.hIcon = static_cast<HICON>(LoadImageW(
            nullptr, IDI_APPLICATION, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    }
    wcscpy_s(nid.szTip, L"EdgeDeck");

    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (m_trayAdded) {
        Diagnostics::Info("Tray: icon added");
    } else {
        // Usually means the notification area does not exist yet - Explorer has
        // not started. TaskbarCreated will arrive and this is retried.
        Diagnostics::Error("Tray: icon could not be added; waiting for the notification area");
    }
    return m_trayAdded;
}

void App::RemoveTrayIcon() {
    if (!m_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_utilityHwnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayAdded = false;
}

int App::RunMessageLoop() {
    MSG msg{};
    BOOL result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (result == -1) return 1;

        // The settings window is a plain (non-dialog-resource) window built
        // from comctl32 children; IsDialogMessage is what gives it free
        // Tab/arrow/Enter navigation between them without hand-rolled code.
        const HWND settingsHwnd =
            (s_instance && s_instance->m_settingsWindow) ? s_instance->m_settingsWindow->Hwnd()
                                                          : nullptr;
        if (settingsHwnd && IsWindow(settingsHwnd) && IsDialogMessage(settingsHwnd, &msg)) {
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
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
// Static callback. App is a legitimate process-wide singleton (exactly one
// per process), unlike Tab, so a plain static back-pointer is fine here.
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::UtilityProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Before the switch, because the id is only known at runtime.
    if (msg == TaskbarCreatedMessage()) {
        // Explorer rebuilt the notification area and dropped our icon along with
        // everyone else's. Re-add it, or the app keeps running with no way to be
        // seen in the tray until it is restarted.
        if (s_instance) {
            // Delete before adding. NIM_ADD does not update an icon that is
            // already registered under the same hWnd/uID - it fails - so a plain
            // re-add only works when the shell really did drop the icon. The
            // delete is a no-op in that case and keeps m_trayAdded honest either
            // way, so the real delete on exit is never suppressed.
            s_instance->RemoveTrayIcon();
            s_instance->AddTrayIcon();
        }
        return 0;
    }

    switch (msg) {
        case WM_HOTKEY:
            if (!s_instance) break;
            if (wParam == kExitHotkeyId) {
                s_instance->RequestExit();
            } else if (wParam >= kOpenHotkeyBase &&
                       wParam < kOpenHotkeyBase + kOpenHotkeyCount) {
                s_instance->OpenTabByIndex(
                    static_cast<size_t>(wParam - kOpenHotkeyBase));
            } else if (wParam >= kDisplayHotkeyBase &&
                       wParam < kDisplayHotkeyBase + kDisplayHotkeyCount) {
                s_instance->SwitchDisplays(static_cast<int>(wParam - kDisplayHotkeyBase));
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
        case kTrayCallbackMessage: {
            // Notification-area icon. Left click opens Settings, right click
            // the menu. This is the safety net: without it, a user whose tabs
            // are off-screen (a monitor disconnected at the wrong moment) or who
            // has removed every tab has no way to reach Settings or Exit at all.
            if (!s_instance) break;
            if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                s_instance->OpenSettings();
            } else if (LOWORD(lParam) == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, kMenuIdSettings, L"Settings...");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kMenuIdExit, L"Exit");

                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                const UINT cmd = static_cast<UINT>(
                    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y,
                                   0, hwnd, nullptr));
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(menu);
                if (cmd == kMenuIdSettings) s_instance->OpenSettings();
                if (cmd == kMenuIdExit) s_instance->RequestExit();
            }
            return 0;
        }
        case WM_DESTROY:
            if (s_instance) {
                s_instance->RemoveTrayIcon();
                for (int i = 0; i < kOpenHotkeyCount; ++i) {
                    UnregisterHotKey(hwnd, kOpenHotkeyBase + i);
                }
                for (int i = 0; i < kDisplayHotkeyCount; ++i) {
                    UnregisterHotKey(hwnd, kDisplayHotkeyBase + i);
                }
                UnregisterHotKey(hwnd, kExitHotkeyId);
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

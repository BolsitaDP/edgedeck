#include "App.h"
#include "AppIcon.h"
#include "Autostart.h"
#include "CrashHandler.h"
#include "Diagnostics.h"
#include "Tab.h"
#include "ActionsWindow.h"
#include "QuickActionsWidget.h"
#include "MediaWidget.h"
#include "BrightnessWidget.h"
#include "LyricsWidget.h"
#include "VolumeWidget.h"
#include "DisplayWidget.h"
#include "SettingsWindow.h"

#include <shellapi.h>

#include <algorithm>

#include "Version.h"

namespace {
const wchar_t kUtilityClassName[] = L"EdgeDeckUtilityWindow";
constexpr UINT kMenuIdExit = 100;
constexpr UINT kMenuIdSettings = 101;
constexpr UINT kTrayId = 102;
constexpr UINT kMenuIdActions = 110;
constexpr UINT kMenuIdPause = 111;
constexpr UINT kMenuIdAutostart = 112;
constexpr UINT kMenuIdOpenLog = 113;
constexpr UINT kMenuIdRestart = 114;

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
    config.edge = s.edge;
    config.monitorId = s.monitor;
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

void App::NoteRestartedAfterCrash() {
    Diagnostics::Info("Started after the previous copy crashed (see the Crash lines above)");
    // Ahead of any other startup notice: it is the more important one, and a second balloon
    // replaces the first, so the two are joined into one message.
    const std::wstring crash = L"EdgeDeck closed unexpectedly and was restarted. Details are in its log.";
    m_startupNotice = m_startupNotice.empty() ? crash : crash + L" " + m_startupNotice;
}

bool App::Create(HINSTANCE hInstance) {
    s_instance = this;
    m_hInstance = hInstance;

    if (!Tab::RegisterClasses(hInstance)) return false;
    if (!CreateUtilityWindow(hInstance)) return false;

    m_settings = Config::LoadOrDefault();
    CrashHandler::SetRestartEnabled(Config::RestartAfterCrash()); // the config is read now, not at install
    if (!BuildTabsFrom(m_settings, hInstance)) return false;
    UpdateDisplayHotkeys();
    CheckAutostart();
    AddTrayIcon();
    if (!m_startupNotice.empty()) ShowBalloon(m_startupNotice.c_str());
    return true;
}

void App::ShowBalloon(const wchar_t* text, bool warning) {
    if (!text || !*text) return;
    if (!m_trayAdded || !m_utilityHwnd) {
        // Explorer has not (re)created the notification area, so there is nothing
        // to show it from. The log is all that is left.
        Diagnostics::Error("Notice: no notification-area icon to show a message from");
        return;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_utilityHwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    // Quiet time is respected: a failed monitor switch is not worth breaking
    // Focus Assist for. NOSOUND is not set because the user's own notification
    // settings already decide that.
    nid.dwInfoFlags = (warning ? NIIF_WARNING : NIIF_INFO) | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(nid.szInfoTitle, L"EdgeDeck");
    wcsncpy_s(nid.szInfo, text, _TRUNCATE); // 255 characters at most
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        Diagnostics::Error("Notice: the notification could not be shown (%s)",
                           Diagnostics::LastErrorText().c_str());
    }
}

void App::CheckAutostart() {
    // If the startup entry points at a file that is gone - the exe was moved, a
    // build folder cleaned - every sign-in would fail without a word. This costs one
    // registry read, and rewrites nothing unless the entry is actually broken; it
    // never creates an entry the user did not ask for, and never takes over one that
    // points at a different copy that still exists.
    const Autostart::State before = Autostart::Inspect();
    const Autostart::Repair repair = Autostart::RepairPath(Autostart::Default(), Autostart::ThisExePath(),
                                                           Autostart::InstalledPath());
    switch (repair) {
        case Autostart::Repair::Repointed:
            Diagnostics::Info("Autostart: the startup entry pointed at a missing file and was repaired");
            break;
        case Autostart::Repair::Failed:
            Diagnostics::Error("Autostart: the startup entry points at a missing file and could not be repaired");
            break;
        default:
            break;
    }

    // Worth a word to the user? A fault is always said; "it is not set up" is said once, and only
    // from the installed copy (a build folder not being on the startup list is how it should be).
    const std::wstring installed = Autostart::InstalledPath();
    const bool installedCopy =
        !installed.empty() && _wcsicmp(installed.c_str(), Autostart::ThisExePath().c_str()) == 0;
    const Autostart::StartupNote note =
        Autostart::NoteForStartup(before, repair, Config::StartupHintShown(), installedCopy);
    if (!note.text.empty()) {
        if (!m_startupNotice.empty()) m_startupNotice += L" ";
        m_startupNotice += note.text;
    }
    if (note.settlesHint) {
        Config::SetStartupHintShown(true);
        if (!Config::Save(m_settings)) Diagnostics::Error("Config: could not record that the startup hint was shown");
    }
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
    if (m_paused) {
        for (auto& tab : m_tabs) tab->SetPaused(true); // a rebuild must not bring them back
    }
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
                             },
                             [this](const wchar_t* problem) { ShowBalloon(problem); },
                             [this] { OpenActionsEditor(); });
}

void App::ApplySettings(const std::vector<TabSettings>& settings) {
    m_settings = settings;
    if (!Config::Save(settings)) {
        // Saving is the one operation whose failure the user would otherwise
        // never find out about: the tabs would move and then quietly revert on
        // the next launch.
        ShowBalloon(L"EdgeDeck could not write its settings file. Your changes will apply for "
                    L"this session but will not be kept.");
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
        ShowBalloon(L"EdgeDeck could not rebuild its panels. The previous layout is still in place.");
        return;
    }
    // A Displays tab may have just been added or removed.
    UpdateDisplayHotkeys();
}

void App::OpenActionsEditor() {
    if (m_actionsWindow && IsWindow(m_actionsWindow->Hwnd())) {
        SetForegroundWindow(m_actionsWindow->Hwnd());
        return;
    }
    m_actionsWindow = std::make_unique<ActionsWindow>();
    m_actionsWindow->Create(m_hInstance, Config::QuickActionList(),
                            [this](std::vector<QuickAction> list) { ApplyActions(std::move(list)); });
}

void App::ApplyActions(std::vector<QuickAction> actions) {
    Config::SetQuickActionList(std::move(actions));
    if (!Config::Save(m_settings)) {
        ShowBalloon(L"EdgeDeck could not write its settings file. Your actions will apply for "
                    L"this session but will not be kept.");
    }
    // Every Quick Actions panel shows the list, and a different number of rows is a different
    // height, so each is told and laid out again - in place, so an open panel stays open.
    for (auto& tab : m_tabs) {
        if (tab->WidgetType() == WidgetType::QuickActions) tab->RefreshWidget();
    }
}

void App::TogglePaused() {
    m_paused = !m_paused;
    for (auto& tab : m_tabs) tab->SetPaused(m_paused);
    UpdateTrayTip();
    Diagnostics::Info(m_paused ? "Tabs paused" : "Tabs resumed");
}

void App::UpdateTrayTip() {
    if (!m_trayAdded || !m_utilityHwnd) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_utilityHwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_TIP;
    wcscpy_s(nid.szTip, m_paused ? L"EdgeDeck " EDGEDECK_VERSION_WSTRING L" (paused)"
                                 : L"EdgeDeck " EDGEDECK_VERSION_WSTRING);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ToggleAutostart() {
    const bool wasEnabled = Autostart::IsEnabled();
    if (Autostart::SetEnabled(!wasEnabled)) {
        ShowBalloon(wasEnabled ? L"EdgeDeck will no longer start with Windows."
                               : L"EdgeDeck will start with Windows.",
                    false);
    } else {
        // Read back after the write, so this is "Windows did not take it" and not a guess.
        ShowBalloon(wasEnabled ? L"Could not turn off Start with Windows: Windows would not let "
                                 L"the startup entry be removed."
                               : L"Could not turn on Start with Windows: Windows would not accept "
                                 L"the startup entry.");
    }
}

void App::OpenLog() {
    const std::wstring log = Diagnostics::LogPath();
    const INT_PTR result = log.empty() ? 0
                                       : reinterpret_cast<INT_PTR>(ShellExecuteW(
                                             nullptr, L"open", log.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) ShowBalloon(L"EdgeDeck could not open its log file.");
}

void App::Restart() {
    const std::wstring exe = Autostart::ThisExePath();
    if (exe.empty()) {
        ShowBalloon(L"EdgeDeck could not work out where it is running from to restart.");
        return;
    }
    // The command line this copy was started with, so whatever it was given is given again,
    // plus the process to wait for: this one still holds the single-instance lock until it has
    // exited, and a copy that finds it taken just quits.
    wchar_t command[2048];
    if (!CrashHandler::BuildRelaunchCommandLine(GetCommandLineW(), GetCurrentProcessId(), command,
                                                sizeof(command) / sizeof(command[0]))) {
        ShowBalloon(L"EdgeDeck could not build the command to restart itself. It is still running.");
        return;
    }
    const std::wstring directory = exe.substr(0, exe.find_last_of(L'\\'));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command, nullptr, nullptr, FALSE, 0, nullptr,
                        directory.empty() ? nullptr : directory.c_str(), &startup, &process)) {
        Diagnostics::Error("Restart: could not start a new copy (%s)", Diagnostics::LastErrorText().c_str());
        ShowBalloon(L"EdgeDeck could not start a new copy of itself. It is still running.");
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    RequestExit();
}
void App::RunMenuCommand(UINT command) {
    switch (command) {
        case kMenuIdSettings: OpenSettings(); break;
        case kMenuIdActions: OpenActionsEditor(); break;
        case kMenuIdPause: TogglePaused(); break;
        case kMenuIdAutostart: ToggleAutostart(); break;
        case kMenuIdOpenLog: OpenLog(); break;
        case kMenuIdRestart: Restart(); break;
        case kMenuIdExit: RequestExit(); break;
        default: break;
    }
}

void App::ShowMenu(POINT screenPt, HWND owner) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuIdSettings, L"Settings...");
    AppendMenuW(menu, MF_STRING, kMenuIdActions, L"Edit quick actions...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuIdPause,
                m_paused ? L"Resume tabs\tCtrl+Alt+P" : L"Pause tabs\tCtrl+Alt+P");
    // Ticked from what Windows will really do (an entry Task Manager switched off is not on).
    AppendMenuW(menu, MF_STRING | (Autostart::IsEnabled() ? MF_CHECKED : 0), kMenuIdAutostart,
                L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuIdOpenLog, L"Open log");
    AppendMenuW(menu, MF_STRING, kMenuIdRestart, L"Restart EdgeDeck");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuIdExit, L"Exit");

    // Standard trick for a NOACTIVATE-owning window's popup menu: briefly take the foreground so
    // the menu dismisses correctly on an outside click, then post WM_NULL per MSDN so
    // TrackPopupMenu's state settles. The command comes back as the return value, so the tab's
    // menu and the tray's are handled by the same code.
    SetForegroundWindow(owner);
    const UINT command = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, 0, owner, nullptr));
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command != 0) RunMenuCommand(command);
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
        // Held until the tray icon exists. This used to be a MessageBox, raised
        // here, before the message loop and before any tab: the app sat invisible
        // behind a dialog until someone found and dismissed it. The tray menu
        // still offers Exit, so the shortcut being taken is worth a note, not a stop.
        // Appended, not assigned: a restart after a crash may already have a note waiting.
        if (!m_startupNotice.empty()) m_startupNotice += L" ";
        m_startupNotice += L"The exit shortcut (Ctrl+Alt+Q) is already taken by another program. "
                           L"You can still exit from the EdgeDeck icon in the notification area.";
        Diagnostics::Error("Hotkey: Ctrl+Alt+Q is already taken by another program");
    }

    for (int i = 0; i < kOpenHotkeyCount; ++i) {
        RegisterHotKey(m_utilityHwnd, kOpenHotkeyBase + i, MOD_CONTROL | MOD_ALT, '1' + i);
    }
    // Logged, not shown: another program owning the chord is not worth a dialog, and the tray
    // menu has the same command.
    if (!RegisterHotKey(m_utilityHwnd, kPauseHotkeyId, MOD_CONTROL | MOD_ALT, 'P')) {
        Diagnostics::Error("Hotkey: Ctrl+Alt+P is already taken by another program");
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
    nid.hIcon = AppIcon::Load(instance, true);
    wcscpy_s(nid.szTip, m_paused ? L"EdgeDeck " EDGEDECK_VERSION_WSTRING L" (paused)"
                                 : L"EdgeDeck " EDGEDECK_VERSION_WSTRING);

    m_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    // The shell keeps its own copy of the image; this one is ours to release. (Add is retried
    // whenever Explorer restarts, so leaving it would leak an icon each time.)
    if (nid.hIcon) DestroyIcon(nid.hIcon);
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
        const HWND actionsHwnd =
            (s_instance && s_instance->m_actionsWindow) ? s_instance->m_actionsWindow->Hwnd() : nullptr;
        if (actionsHwnd && IsWindow(actionsHwnd) && IsDialogMessage(actionsHwnd, &msg)) {
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::ShowTabContextMenu(Tab* /*tab*/, POINT screenPt) {
    ShowMenu(screenPt, m_utilityHwnd);
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
            } else if (wParam == kPauseHotkeyId) {
                s_instance->TogglePaused();
            } else if (wParam >= kOpenHotkeyBase &&
                       wParam < kOpenHotkeyBase + kOpenHotkeyCount) {
                s_instance->OpenTabByIndex(
                    static_cast<size_t>(wParam - kOpenHotkeyBase));
            } else if (wParam >= kDisplayHotkeyBase &&
                       wParam < kDisplayHotkeyBase + kDisplayHotkeyCount) {
                s_instance->SwitchDisplays(static_cast<int>(wParam - kDisplayHotkeyBase));
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
                POINT pt;
                GetCursorPos(&pt);
                s_instance->ShowMenu(pt, hwnd);
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
                UnregisterHotKey(hwnd, kPauseHotkeyId);
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

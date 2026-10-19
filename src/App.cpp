#include "App.h"
#include "AppIcon.h"
#include "Autostart.h"
#include "BugReport.h"
#include "CrashHandler.h"
#include "Diagnostics.h"
#include "Tab.h"
#include "ActionsWindow.h"
#include "QuickActionsWidget.h"
#include "RecentFilesWidget.h"
#include "MediaWidget.h"
#include "BrightnessWidget.h"
#include "LyricsWidget.h"
#include "VolumeWidget.h"
#include "DisplayWidget.h"
#include "SettingsWindow.h"
#include "ShortcutsWindow.h"
#include "LayoutsWindow.h"
#include "Strings.h"

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
constexpr UINT kMenuIdUpdates = 115;
constexpr UINT kMenuIdReport = 116;

// Ctrl+Alt+Q: the same gesture as the per-tab openers, so it cannot collide
// with them, and a chord nothing else claims.


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

// What a second copy sends the running one (see AskRunningCopyToShowItself). Registered, so it
// cannot collide with anyone else's message number.
UINT ShowYourselfMessage() {
    static const UINT msg = RegisterWindowMessageW(L"EdgeDeck.ShowYourself");
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
        case WidgetType::RecentFiles: return std::make_unique<RecentFilesWidget>();
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
    // Said once the settings are read, in their language (see Create).
    m_restartedAfterCrash = true;
}

void App::AddStartupNotice(const std::wstring& text, bool warning) {
    if (text.empty()) return;
    if (!m_startupNotice.empty()) m_startupNotice += L" ";
    m_startupNotice += text;
    m_startupNoticeWarns = m_startupNoticeWarns || warning;
}

// A settings file that could not be read, or was written by a newer EdgeDeck, has been copied aside
// by Config before anything writes over it; the user is told, since what they see is not what they
// set.
void App::NoteConfigProblem() {
    const Config::LoadProblem problem = Config::LastLoadProblem();
    if (problem == Config::LoadProblem::None) return;
    const std::wstring backup = Config::LastBackupPath();
    Diagnostics::Error("Config: %s; %s", problem == Config::LoadProblem::Newer ? "written by a newer version"
                                                                              : "could not be read, using defaults",
                       backup.empty() ? "no copy could be made" : "copied to config.backup.txt");
    AddStartupNotice(problem == Config::LoadProblem::Newer ? TR(ConfigNewer) : TR(ConfigUnreadable));
    if (!backup.empty()) AddStartupNotice(Strings::Format(Strings::Id::ConfigBackupKept, Config::kBackupFileName));
}

bool App::Create(HINSTANCE hInstance) {
    s_instance = this;
    m_hInstance = hInstance;

    if (!Tab::RegisterClasses(hInstance)) return false;
    if (!CreateUtilityWindow(hInstance)) return false;

    // No settings file yet: this is the first start on this account. Asked before anything below
    // writes one.
    const std::wstring configPath = Config::FilePath();
    m_firstRun = !configPath.empty() && GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES;

    m_settings = Config::LoadOrDefault(); // also sets the language
    m_language = Strings::Current();
    CrashHandler::SetRestartEnabled(Config::RestartAfterCrash()); // the config is read now, not at install
    // Notes found before the settings were read are worded now. A second balloon replaces the
    // first, so they are joined into one message, the crash first: it is the more important one.
    if (m_restartedAfterCrash) AddStartupNotice(TR(CrashRestarted));
    NoteConfigProblem();
    // A first start says what EdgeDeck is and where it is - a few small tabs on the screen edge are
    // easy to miss - as a notification, not a window: nothing EdgeDeck does takes the focus.
    // Started straight from the zip, EdgeDeck runs from a folder Windows cleans later. That is the
    // thing to say then - in place of the welcome, which would not fit beside it in one notification.
    const bool fromTemp = Autostart::IsInTemporaryFolder(Autostart::ThisExePath());
    if (fromTemp) {
        Diagnostics::Info("Started from a temporary folder (probably inside a zip)");
        AddStartupNotice(TR(RunningFromTemp));
    } else if (m_firstRun) {
        AddStartupNotice(TR(Welcome), false);
    }
    CarryOverDisplayLayouts();
    if (!BuildTabsFrom(m_settings, hInstance)) return false;
    // After the tabs: the display shortcuts exist only while there is a Displays tab.
    RegisterShortcuts();
    // The exit shortcut being taken is worth a note (the tray menu still has Exit); the others
    // are in the log, and in the Shortcuts window when it is used.
    if (m_exitShortcutTaken) {
        AddStartupNotice(Strings::Format(Strings::Id::ExitHotkeyTaken,
                                         Shortcuts::Display(Config::ShortcutSet().exit).c_str()));
    }
    CheckAutostart();
    AddTrayIcon();
    if (!m_startupNotice.empty()) ShowBalloon(m_startupNotice.c_str(), m_startupNoticeWarns);
    return true;
}

void App::CarryOverDisplayLayouts() {
    // The Displays tab used to have four fixed buttons. Someone who had the tab keeps them, as
    // saved layouts with the same names in the same order - the order of their shortcuts - worked
    // out once from the monitors present now. Done once: removing them all later means "the
    // automatic ones", not "bring the old ones back".
    if (Config::DisplayLayoutsCarried()) return;
    const bool hasDisplaysTab = std::any_of(m_settings.begin(), m_settings.end(), [](const TabSettings& t) {
        return t.widgetType == WidgetType::Displays;
    });
    if (hasDisplaysTab && Config::DisplayLayoutList().empty()) {
        const std::vector<DisplayTopology::Monitor> monitors = DisplayTopology::Enumerate();
        const std::wstring tvId = DisplayTopology::ResolveTvId(monitors, Config::DisplayTvMonitor());
        const std::wstring mainId = DisplayTopology::ResolveMainId(monitors, tvId, Config::DisplayMainMonitor());
        Config::SetDisplayLayoutList(DisplayLayouts::FromProfiles(monitors, tvId, mainId));
        Diagnostics::Info("Displays: carried the four fixed layouts over as %zu saved ones",
                          Config::DisplayLayoutList().size());
    }
    Config::SetDisplayLayoutsCarried(true);
    if (!Config::Save(m_settings)) Diagnostics::Error("Config: could not save the carried-over display layouts");
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
    // The one-time "not set to start with Windows" hint waits for the second start rather than
    // share the first start's one notification with the welcome; a fault is said either way.
    if (m_firstRun && note.settlesHint) return;
    AddStartupNotice(note.text);
    if (note.settlesHint) {
        Config::SetStartupHintShown(true);
        if (!Config::Save(m_settings)) Diagnostics::Error("Config: could not record that the startup hint was shown");
    }
}
void App::UpdateDisplayHotkeys(std::vector<std::wstring>* taken) {
    static_assert(kDisplayHotkeyCount == static_cast<int>(DisplayLayouts::kMaxLayouts),
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
    const UINT modifiers = Config::ShortcutSet().displayModifiers;
    if (!hasDisplaysTab || modifiers == 0) return;

    bool anyTaken = false;
    for (int i = 0; i < kDisplayHotkeyCount; ++i) {
        if (!RegisterHotKey(m_utilityHwnd, kDisplayHotkeyBase + i, modifiers, '1' + i)) anyTaken = true;
    }
    if (anyTaken) {
        // Logged, not shown: another program owning a chord is not worth a dialog, and the tab's
        // buttons still work. The Shortcuts window says so when it asked.
        const std::wstring shown = Shortcuts::DisplayFamily(modifiers, kDisplayHotkeyCount);
        Diagnostics::Error("Hotkey: %ls is already taken by another program", shown.c_str());
        if (taken) taken->push_back(shown);
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
                             [this] { OpenActionsEditor(); }, [this] { OpenShortcutsEditor(); },
                             [this] { OpenLayoutsEditor(); });
}

void App::ApplySettings(const std::vector<TabSettings>& settings) {
    m_settings = settings;
    if (!Config::Save(settings)) {
        // Saving is the one operation whose failure the user would otherwise
        // never find out about: the tabs would move and then quietly revert on
        // the next launch.
        ShowBalloon(TR(SaveFailedSettings));
    }

    // A new language: the tray tip, and every widget that keeps text it worked out earlier (the
    // default quick actions' names, "5 min ago") is told, so nothing stays in the old one.
    const bool languageChanged = Strings::Current() != m_language;
    m_language = Strings::Current();
    UpdateTrayTip();

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
                // The folder a Recent files tab lists is a setting of its own and may have
                // changed in this Save; the widget is kept, so it has to be told.
                if (languageChanged || settings[i].widgetType == WidgetType::RecentFiles) m_tabs[i]->RefreshWidget();
            }
            return;
        }
    }

    if (!BuildTabsFrom(settings, m_hInstance)) {
        ShowBalloon(TR(RebuildFailed));
        return;
    }
    // A Displays tab may have just been added or removed.
    UpdateDisplayHotkeys();
}

void App::OnTabMoved(const Tab* tab, float oldRatio, float newRatio) {
    const auto found = std::find_if(m_tabs.begin(), m_tabs.end(), [tab](const auto& t) { return t.get() == tab; });
    const size_t index = static_cast<size_t>(found - m_tabs.begin());
    if (found == m_tabs.end() || index >= m_settings.size()) return;
    m_settings[index].verticalRatio = newRatio;
    if (!Config::Save(m_settings)) ShowBalloon(TR(SaveFailedSettings));
    if (m_settingsWindow && IsWindow(m_settingsWindow->Hwnd())) {
        m_settingsWindow->TabMoved(index, m_settings[index].widgetType, oldRatio, newRatio);
    }
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

void App::OpenLayoutsEditor() {
    if (m_layoutsWindow && IsWindow(m_layoutsWindow->Hwnd())) {
        SetForegroundWindow(m_layoutsWindow->Hwnd());
        return;
    }
    m_layoutsWindow = std::make_unique<LayoutsWindow>();
    m_layoutsWindow->Create(m_hInstance, Config::DisplayLayoutList(),
                            [this](std::vector<DisplayLayouts::Layout> layouts) { ApplyLayouts(std::move(layouts)); });
}

void App::ApplyLayouts(std::vector<DisplayLayouts::Layout> layouts) {
    Config::SetDisplayLayoutList(std::move(layouts));
    if (!Config::Save(m_settings)) ShowBalloon(TR(SaveFailedSettings));
    for (auto& tab : m_tabs) {
        if (tab->WidgetType() == WidgetType::Displays) tab->RefreshWidget();
    }
}

void App::OpenShortcutsEditor() {
    if (m_shortcutsWindow && IsWindow(m_shortcutsWindow->Hwnd())) {
        SetForegroundWindow(m_shortcutsWindow->Hwnd());
        return;
    }
    m_shortcutsWindow = std::make_unique<ShortcutsWindow>();
    m_shortcutsWindow->Create(m_hInstance, Config::ShortcutSet(),
                              [this](const Shortcuts::Set& set) { return ApplyShortcuts(set); });
}

std::wstring App::ApplyShortcuts(const Shortcuts::Set& set) {
    Config::SetShortcutSet(set);
    if (!Config::Save(m_settings)) ShowBalloon(TR(SaveFailedSettings));
    const std::vector<std::wstring> taken = RegisterShortcuts();
    // The Settings window, if open, lists the keys in its footer.
    if (m_settingsWindow && IsWindow(m_settingsWindow->Hwnd())) m_settingsWindow->RefreshKeyboardHelp();
    if (taken.empty()) return {};
    std::wstring list;
    for (const std::wstring& shown : taken) list += (list.empty() ? L"" : L", ") + shown;
    return Strings::Format(Strings::Id::ShortcutTaken, list.c_str());
}

void App::ApplyActions(std::vector<QuickAction> actions) {
    Config::SetQuickActionList(std::move(actions));
    if (!Config::Save(m_settings)) {
        ShowBalloon(TR(SaveFailedActions));
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
    wcsncpy_s(nid.szTip, TrayTip().c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

std::wstring App::TrayTip() const {
    const wchar_t* name = L"EdgeDeck " EDGEDECK_VERSION_WSTRING;
    return m_paused ? Strings::Format(Strings::Id::TrayPaused, name) : std::wstring(name);
}

void App::ToggleAutostart() {
    const bool wasEnabled = Autostart::IsEnabled();
    // An entry for a copy in a temporary folder would break as soon as Windows cleans it.
    if (!wasEnabled && Autostart::IsInTemporaryFolder(Autostart::PreferredPath())) {
        ShowBalloon(TR(AutostartFromTemp));
        return;
    }
    if (Autostart::SetEnabled(!wasEnabled)) {
        ShowBalloon(wasEnabled ? TR(AutostartNowOff) : TR(AutostartNowOn), false);
    } else {
        // Read back after the write, so this is "Windows did not take it" and not a guess.
        ShowBalloon(wasEnabled ? TR(AutostartOffFailed) : TR(AutostartOnFailed));
    }
}

void App::OpenLog() {
    const std::wstring log = Diagnostics::LogPath();
    const INT_PTR result = log.empty() ? 0
                                       : reinterpret_cast<INT_PTR>(ShellExecuteW(
                                             nullptr, L"open", log.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) ShowBalloon(TR(OpenLogFailed));
}

void App::OpenReleasesPage() {
    // No checking in the background - that would be network traffic nobody asked for, every day.
    // The page says what the newest version is; the tooltip says which one is running.
    const wchar_t* url = EDGEDECK_HOMEPAGE_WSTRING L"/releases/latest";
    const INT_PTR result =
        reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) ShowBalloon(TR(UpdatesOpenFailed));
}

void App::ReportProblem() {
    const BugReport::Facts facts = BugReport::Gather(EDGEDECK_VERSION_WSTRING);
    const bool copied = CopyToClipboard(BugReport::ClipboardText(facts));
    const std::wstring url = BugReport::IssueUrl(EDGEDECK_HOMEPAGE_WSTRING, facts);
    const INT_PTR result =
        reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        ShowBalloon(TR(ReportOpenFailed));
    } else {
        ShowBalloon(copied ? TR(ReportLogCopied) : TR(ReportNoClipboard), !copied);
    }
}

bool App::CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(m_utilityHwnd)) return false;
    bool done = false;
    if (EmptyClipboard()) {
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* target = GlobalLock(memory)) {
                memcpy(target, text.c_str(), bytes);
                GlobalUnlock(memory);
                // The clipboard owns the memory once it takes it, and only then.
                done = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            }
            if (!done) GlobalFree(memory);
        }
    }
    CloseClipboard();
    if (!done) Diagnostics::Error("Report: could not put the log on the clipboard (%s)",
                                  Diagnostics::LastErrorText().c_str());
    return done;
}

void App::Restart() {
    const std::wstring exe = Autostart::ThisExePath();
    if (exe.empty()) {
        ShowBalloon(TR(RestartNoPath));
        return;
    }
    // The command line this copy was started with, so whatever it was given is given again,
    // plus the process to wait for: this one still holds the single-instance lock until it has
    // exited, and a copy that finds it taken just quits.
    wchar_t command[2048];
    if (!CrashHandler::BuildRelaunchCommandLine(GetCommandLineW(), GetCurrentProcessId(), command,
                                                sizeof(command) / sizeof(command[0]))) {
        ShowBalloon(TR(RestartNoCommand));
        return;
    }
    const std::wstring directory = exe.substr(0, exe.find_last_of(L'\\'));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command, nullptr, nullptr, FALSE, 0, nullptr,
                        directory.empty() ? nullptr : directory.c_str(), &startup, &process)) {
        Diagnostics::Error("Restart: could not start a new copy (%s)", Diagnostics::LastErrorText().c_str());
        ShowBalloon(TR(RestartNoStart));
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
        case kMenuIdUpdates: OpenReleasesPage(); break;
        case kMenuIdReport: ReportProblem(); break;
        case kMenuIdExit: RequestExit(); break;
        default: break;
    }
}

void App::ShowMenu(POINT screenPt, HWND owner) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuIdSettings, TR(MenuSettings));
    AppendMenuW(menu, MF_STRING, kMenuIdActions, TR(MenuActions));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // The menu shows the shortcut beside the command, as Windows menus do - the one set now.
    std::wstring pause = m_paused ? TR(MenuResume) : TR(MenuPause);
    const Shortcut pauseKeys = Config::ShortcutSet().pause;
    if (!pauseKeys.Off()) pause += L"\t" + Shortcuts::Display(pauseKeys);
    AppendMenuW(menu, MF_STRING, kMenuIdPause, pause.c_str());
    // Ticked from what Windows will really do (an entry Task Manager switched off is not on).
    AppendMenuW(menu, MF_STRING | (Autostart::IsEnabled() ? MF_CHECKED : 0), kMenuIdAutostart,
                TR(StartWithWindows));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuIdOpenLog, TR(MenuOpenLog));
    AppendMenuW(menu, MF_STRING, kMenuIdRestart, TR(MenuRestart));
    AppendMenuW(menu, MF_STRING, kMenuIdUpdates, TR(MenuUpdates));
    AppendMenuW(menu, MF_STRING, kMenuIdReport, TR(MenuReport));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuIdExit, TR(MenuExit));

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

    // The shortcuts are registered once the settings say which they are (RegisterShortcuts).
    return true;
}

std::vector<std::wstring> App::RegisterShortcuts() {
    std::vector<std::wstring> taken;
    if (!m_utilityHwnd) return taken;
    const Shortcuts::Set set = Config::ShortcutSet();

    // Everything is dropped first, so this one call serves start-up and every change;
    // unregistering a key that was never registered is a no-op.
    UnregisterHotKey(m_utilityHwnd, kExitHotkeyId);
    UnregisterHotKey(m_utilityHwnd, kPauseHotkeyId);
    for (int i = 0; i < kOpenHotkeyCount; ++i) UnregisterHotKey(m_utilityHwnd, kOpenHotkeyBase + i);

    // Another program owning a chord is not worth a dialog: it is logged, said in the Shortcuts
    // window, and - for exit only - said once at start-up. The menu has every command anyway.
    auto noteTaken = [&](const std::wstring& shown) {
        Diagnostics::Error("Hotkey: %ls is already taken by another program", shown.c_str());
        taken.push_back(shown);
    };
    m_exitShortcutTaken = false;
    if (!set.exit.Off() && !RegisterHotKey(m_utilityHwnd, kExitHotkeyId, set.exit.modifiers, set.exit.key)) {
        m_exitShortcutTaken = true;
        noteTaken(Shortcuts::Display(set.exit));
    }
    if (!set.pause.Off() && !RegisterHotKey(m_utilityHwnd, kPauseHotkeyId, set.pause.modifiers, set.pause.key)) {
        noteTaken(Shortcuts::Display(set.pause));
    }
    if (set.tabModifiers != 0) {
        bool anyTaken = false;
        for (int i = 0; i < kOpenHotkeyCount; ++i) {
            if (!RegisterHotKey(m_utilityHwnd, kOpenHotkeyBase + i, set.tabModifiers, '1' + i)) anyTaken = true;
        }
        if (anyTaken) noteTaken(Shortcuts::DisplayFamily(set.tabModifiers, kOpenHotkeyCount));
    }
    UpdateDisplayHotkeys(&taken);
    return taken;
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
    wcsncpy_s(nid.szTip, TrayTip().c_str(), _TRUNCATE);

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
        const HWND layoutsHwnd =
            (s_instance && s_instance->m_layoutsWindow) ? s_instance->m_layoutsWindow->Hwnd() : nullptr;
        if (layoutsHwnd && IsWindow(layoutsHwnd) && IsDialogMessage(layoutsHwnd, &msg)) {
            continue;
        }
        const HWND shortcutsHwnd =
            (s_instance && s_instance->m_shortcutsWindow) ? s_instance->m_shortcutsWindow->Hwnd() : nullptr;
        if (shortcutsHwnd && IsWindow(shortcutsHwnd) && IsDialogMessage(shortcutsHwnd, &msg)) {
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

bool App::AskRunningCopyToShowItself() {
    HWND running = FindWindowW(kUtilityClassName, nullptr);
    if (!running) return false;
    // This copy was just started by the user, so it may hand its right to the foreground on: without
    // that, Windows would keep the Settings window behind whatever is in front.
    DWORD pid = 0;
    GetWindowThreadProcessId(running, &pid);
    if (pid) AllowSetForegroundWindow(pid);
    return PostMessageW(running, ShowYourselfMessage(), 0, 0) != FALSE;
}

LRESULT CALLBACK App::UtilityProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == ShowYourselfMessage()) {
        // EdgeDeck was started again while running: Settings is where someone who went looking
        // for it can see that it is, and change anything.
        if (s_instance) {
            Diagnostics::Info("Started again while running: showing Settings");
            s_instance->OpenSettings();
        }
        return 0;
    }
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

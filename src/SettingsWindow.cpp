#include "SettingsWindow.h"
#include "Autostart.h"

#include <commctrl.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {
constexpr int kIdList = 100;
constexpr int kIdTypeCombo = 101;
constexpr int kIdTabWidth = 102;
constexpr int kIdTabHeight = 103;
constexpr int kIdPanelWidth = 104;
constexpr int kIdAdd = 105;
constexpr int kIdRemove = 106;
constexpr int kIdUp = 107;
constexpr int kIdDown = 108;
constexpr int kIdSave = 109;
constexpr int kIdClose = 110;
constexpr int kIdTrackbar = 111;
constexpr int kIdAutostart = 112;
constexpr int kIdThemeCombo = 113;

struct TypeEntry {
    WidgetType type;
    const wchar_t* comboLabel;
    const wchar_t* listLabel;
};
constexpr TypeEntry kTypes[] = {
    {WidgetType::QuickActions, L"Quick Actions", L"Quick Actions"},
    {WidgetType::Media, L"Media (auto-detect)", L"Media"},
    {WidgetType::Brightness, L"Brightness (monitors)", L"Brightness"},
    {WidgetType::Lyrics, L"Lyrics (auto-detect)", L"Lyrics"},
    {WidgetType::Volume, L"Volume (audio output)", L"Volume"},
};
constexpr int kTypeCount = static_cast<int>(sizeof(kTypes) / sizeof(kTypes[0]));

int TypeIndex(WidgetType type) {
    for (int i = 0; i < kTypeCount; ++i) {
        if (kTypes[i].type == type) return i;
    }
    return 0;
}

WidgetType TypeFromIndex(int index) {
    return (index >= 0 && index < kTypeCount) ? kTypes[index].type : WidgetType::QuickActions;
}

const wchar_t kSettingsClassName[] = L"EdgeDeckSettingsWindow";
} // namespace

bool SettingsWindow::Create(HINSTANCE hInstance, std::vector<TabSettings> initial,
                             SaveCallback onSave) {
    m_tabs = std::move(initial);
    m_onSave = std::move(onSave);

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.hInstance = hInstance;
        wc.lpszClassName = kSettingsClassName;
        wc.lpfnWndProc = WndProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        if (!RegisterClassExW(&wc)) return false;
        classRegistered = true;
    }

    m_hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClassName, L"EdgeDeck Settings",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                             CW_USEDEFAULT, 500, 500, nullptr, nullptr, hInstance, this);
    if (!m_hwnd) return false;

    // Centred on the monitor the pointer is on, or the primary one, so the
    // dialog does not open half off-screen on a multi-monitor setup.
    POINT cursor{};
    if (!GetCursorPos(&cursor)) cursor = POINT{0, 0};
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    RECT windowRect{};
    GetWindowRect(m_hwnd, &windowRect);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfo(monitor, &mi)) {
        const int cx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left) / 2;
        const int cy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top) / 2;
        const int w = windowRect.right - windowRect.left;
        const int h = windowRect.bottom - windowRect.top;
        SetWindowPos(m_hwnd, HWND_TOP, cx - w / 2, cy - h / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

void SettingsWindow::CreateControls(HINSTANCE hInstance) {
    m_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
                     int h, int id) {
        HWND child = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, w, h,
                                      m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      hInstance, nullptr);
        if (child && m_font) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        return child;
    };

    make(L"STATIC", L"Tabs", 0, 12, 10, 100, 18, 0);
    m_list = make(L"LISTBOX", nullptr, WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 12, 30, 190, 360, kIdList);

    m_addButton = make(L"BUTTON", L"Add", BS_PUSHBUTTON, 12, 398, 56, 24, kIdAdd);
    m_removeButton = make(L"BUTTON", L"Remove", BS_PUSHBUTTON, 72, 398, 56, 24, kIdRemove);
    m_upButton = make(L"BUTTON", L"Up", BS_PUSHBUTTON, 132, 398, 34, 24, kIdUp);
    m_downButton = make(L"BUTTON", L"Down", BS_PUSHBUTTON, 170, 398, 42, 24, kIdDown);

    make(L"STATIC", L"Widget type:", 0, 220, 12, 140, 18, 0);
    m_typeCombo = make(L"COMBOBOX", nullptr, WS_BORDER | WS_VSCROLL | CBS_DROPDOWNLIST, 220, 30, 250,
                       200, kIdTypeCombo);
    for (const TypeEntry& entry : kTypes) {
        SendMessageW(m_typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.comboLabel));
    }

    make(L"STATIC", L"Vertical position:", 0, 220, 64, 160, 18, 0);
    m_positionTrackbar = make(TRACKBAR_CLASSW, nullptr, WS_TABSTOP | TBS_HORZ, 220, 82, 190, 28,
                              kIdTrackbar);
    SendMessageW(m_positionTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    m_positionLabel = make(L"STATIC", L"50%", 0, 420, 88, 48, 18, 0);

    make(L"STATIC", L"Tab width (px):", 0, 220, 118, 160, 18, 0);
    m_tabWidthEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 220, 136, 90, 22,
                          kIdTabWidth);

    make(L"STATIC", L"Tab height (px):", 0, 220, 164, 160, 18, 0);
    m_tabHeightEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 220, 182, 90, 22,
                           kIdTabHeight);

    make(L"STATIC", L"Panel width (px):", 0, 220, 210, 160, 18, 0);
    m_panelWidthEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 220, 228, 90, 22,
                            kIdPanelWidth);

    m_autostartCheck = make(L"BUTTON", L"Start with Windows", BS_AUTOCHECKBOX | WS_TABSTOP, 220, 262,
                             240, 22, kIdAutostart);
    SendMessageW(m_autostartCheck, BM_SETCHECK, Autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED,
                 0);

    // Following the system is the default and the reason the panel normally matches
    // the rest of Windows; forcing either mode is for people whose system setting
    // and preferred panel appearance disagree, which is not rare - plenty of
    // people run a light system and still want the dark panel.
    make(L"STATIC", L"Theme:", 0, 220, 292, 60, 18, 0);
    m_themeCombo = make(L"COMBOBOX", nullptr, WS_BORDER | WS_VSCROLL | CBS_DROPDOWNLIST, 280, 290,
                        196, 120, kIdThemeCombo);
    for (const wchar_t* label : {L"Follow Windows", L"Always dark", L"Always light"}) {
        SendMessageW(m_themeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    SendMessageW(m_themeCombo, CB_SETCURSEL, static_cast<WPARAM>(ThemeMode()), 0);

    make(L"STATIC", L"Keyboard", 0, 12, 392, 100, 18, 0);
    make(L"STATIC",
         L"Ctrl+Alt+1..9  open and pin a panel\r\n"
         L"Ctrl+Alt+Q       exit EdgeDeck\r\n"
         L"Arrow keys / wheel  move and adjust inside an open panel\r\n"
         L"Enter / Space    activate the focused control\r\n"
         L"Escape           unpin and close the panel",
         0, 12, 410, 96, 66, 0);

    m_saveButton = make(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, 330, 398, 70, 26, kIdSave);
    m_closeButton = make(L"BUTTON", L"Close", BS_PUSHBUTTON, 406, 398, 70, 26, kIdClose);
}

void SettingsWindow::RefreshList(int selectIndex) {
    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const wchar_t* typeName = kTypes[TypeIndex(m_tabs[i].widgetType)].listLabel;
        wchar_t buf[96];
        swprintf_s(buf, L"%zu: %s", i + 1, typeName);
        SendMessageW(m_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
    }

    if (m_tabs.empty()) {
        m_selectedIndex = -1;
    } else {
        m_selectedIndex = std::clamp(selectIndex, 0, static_cast<int>(m_tabs.size()) - 1);
        SendMessageW(m_list, LB_SETCURSEL, static_cast<WPARAM>(m_selectedIndex), 0);
    }
    LoadSelectedIntoControls();
}

void SettingsWindow::LoadSelectedIntoControls() {
    const bool hasSelection = m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_tabs.size());
    EnableWindow(m_typeCombo, hasSelection);
    EnableWindow(m_positionTrackbar, hasSelection);
    EnableWindow(m_tabWidthEdit, hasSelection);
    EnableWindow(m_tabHeightEdit, hasSelection);
    EnableWindow(m_panelWidthEdit, hasSelection);
    EnableWindow(m_removeButton, hasSelection);
    EnableWindow(m_upButton, hasSelection && m_selectedIndex > 0);
    EnableWindow(m_downButton, hasSelection && m_selectedIndex < static_cast<int>(m_tabs.size()) - 1);

    if (!hasSelection) return;
    const TabSettings& t = m_tabs[m_selectedIndex];

    SendMessageW(m_typeCombo, CB_SETCURSEL, TypeIndex(t.widgetType), 0);

    const int pos = static_cast<int>(std::lround(t.verticalRatio * 100.0f));
    SendMessageW(m_positionTrackbar, TBM_SETPOS, TRUE, pos);
    wchar_t pctBuf[16];
    swprintf_s(pctBuf, L"%d%%", pos);
    SetWindowTextW(m_positionLabel, pctBuf);

    wchar_t buf[32];
    swprintf_s(buf, L"%.0f", t.tabWidth);
    SetWindowTextW(m_tabWidthEdit, buf);
    swprintf_s(buf, L"%.0f", t.tabHeight);
    SetWindowTextW(m_tabHeightEdit, buf);
    swprintf_s(buf, L"%.0f", t.panelWidth);
    SetWindowTextW(m_panelWidthEdit, buf);
}

void SettingsWindow::StoreControlsIntoSelected() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_tabs.size())) return;
    TabSettings& t = m_tabs[m_selectedIndex];

    t.widgetType = TypeFromIndex(static_cast<int>(SendMessageW(m_typeCombo, CB_GETCURSEL, 0, 0)));

    const int pos = static_cast<int>(SendMessageW(m_positionTrackbar, TBM_GETPOS, 0, 0));
    t.verticalRatio = std::clamp(pos, 0, 100) / 100.0f;

    // Each field is parsed strictly and then written back into its own edit, so
    // what the dialog shows always matches what it will save. The old code
    // clamped the model and left the edit displaying the original text, which
    // meant typing "abc" looked like it had been accepted and then quietly
    // became 12.
    //
    // readNumber *writes* the parsed value on success and reports whether it did.
    // It used to only report validity, so the value typed into the box was thrown
    // away and the caller's clamp applied the model's old number instead: the
    // dialog silently discarded tab width, tab height and panel width. A rejected
    // edit now leaves the model untouched, which is the behaviour the comment
    // above describes.
    auto readNumber = [](HWND edit, float& out) {
        wchar_t buf[32];
        GetWindowTextW(edit, buf, 32);
        wchar_t* end = nullptr;
        errno = 0;
        const double value = wcstod(buf, &end);
        if (end == buf || *end != L'\0' || errno == ERANGE || value != value) return false;
        out = static_cast<float>(value);
        return true;
    };

    // These per-field clamps and the ConfigLimits::Clamp below look redundant and
    // are not. They differ in what they do to a value below the minimum:
    // std::clamp raises it to the minimum, while Clamp resets it to the default.
    // The per-field clamp runs first and so rescues what the user typed - typing 5
    // as a tab width gives 12, the smallest allowed value, rather than silently
    // becoming 26. Clamp then has nothing left to reset, and still covers the
    // fields this dialog does not edit.
    if (readNumber(m_tabWidthEdit, t.tabWidth)) {
        t.tabWidth = std::clamp(t.tabWidth, ConfigLimits::kMinTabWidth, ConfigLimits::kMaxTabWidth);
    }
    if (readNumber(m_tabHeightEdit, t.tabHeight)) {
        t.tabHeight =
            std::clamp(t.tabHeight, ConfigLimits::kMinTabHeight, ConfigLimits::kMaxTabHeight);
    }
    if (readNumber(m_panelWidthEdit, t.panelWidth)) {
        t.panelWidth =
            std::clamp(t.panelWidth, ConfigLimits::kMinPanelWidth, ConfigLimits::kMaxPanelWidth);
    }

    ConfigLimits::Clamp(t);

    wchar_t buf[32];
    SetWindowTextW(m_tabWidthEdit, (swprintf_s(buf, L"%.0f", t.tabWidth), buf));
    SetWindowTextW(m_tabHeightEdit, (swprintf_s(buf, L"%.0f", t.tabHeight), buf));
    SetWindowTextW(m_panelWidthEdit, (swprintf_s(buf, L"%.0f", t.panelWidth), buf));
}

void SettingsWindow::OnSelectionChanged() {
    const int sel = static_cast<int>(SendMessageW(m_list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return;
    m_selectedIndex = sel;
    LoadSelectedIntoControls();
}

void SettingsWindow::OnAdd() {
    StoreControlsIntoSelected();
    TabSettings t;
    t.widgetType = WidgetType::QuickActions;
    t.verticalRatio = 0.5f;
    m_tabs.push_back(t);
    RefreshList(static_cast<int>(m_tabs.size()) - 1);
}

void SettingsWindow::OnRemove() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_tabs.size())) return;
    m_tabs.erase(m_tabs.begin() + m_selectedIndex);
    RefreshList(m_selectedIndex);
}

void SettingsWindow::OnMove(int delta) {
    if (m_selectedIndex < 0) return;
    const int target = m_selectedIndex + delta;
    if (target < 0 || target >= static_cast<int>(m_tabs.size())) return;
    StoreControlsIntoSelected();
    std::swap(m_tabs[m_selectedIndex], m_tabs[target]);
    RefreshList(target);
}

void SettingsWindow::OnTrackbarChanged() {
    if (m_selectedIndex < 0) return;
    const int pos = static_cast<int>(SendMessageW(m_positionTrackbar, TBM_GETPOS, 0, 0));
    wchar_t buf[16];
    swprintf_s(buf, L"%d%%", pos);
    SetWindowTextW(m_positionLabel, buf);
}

void SettingsWindow::OnSave() {
    StoreControlsIntoSelected();
    Autostart::SetEnabled(SendMessageW(m_autostartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    // Applied live rather than at next launch: someone who just picked a theme
    // wants to see it, and the palette is cached and compared on every draw, so
    // this is all it takes.
    const LRESULT sel = SendMessageW(m_themeCombo, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) Config::SetCurrentThemeMode(static_cast<ThemeMode>(sel));

    if (m_onSave) m_onSave(m_tabs);
    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self =
        reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // m_hwnd is only assigned once CreateWindowExW returns to Create(),
            // but WM_CREATE fires synchronously from inside that same call, so
            // CreateControls (which parents children off m_hwnd) must use the
            // hwnd handed to us here instead - it's already valid.
            m_hwnd = hwnd;
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            CreateControls(cs->hInstance);
            RefreshList(0);
            return 0;
        }
        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);
            if (id == kIdList && code == LBN_SELCHANGE) {
                StoreControlsIntoSelected();
                OnSelectionChanged();
            } else if (id == kIdTypeCombo && code == CBN_SELCHANGE) {
                StoreControlsIntoSelected();
                RefreshList(m_selectedIndex);
            } else if (id == kIdAdd) {
                OnAdd();
            } else if (id == kIdRemove) {
                OnRemove();
            } else if (id == kIdUp) {
                OnMove(-1);
            } else if (id == kIdDown) {
                OnMove(1);
            } else if (id == kIdSave) {
                OnSave();
            } else if (id == kIdClose) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lParam) == m_positionTrackbar) OnTrackbarChanged();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (m_font) {
                DeleteObject(m_font);
                m_font = nullptr;
            }
            m_hwnd = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

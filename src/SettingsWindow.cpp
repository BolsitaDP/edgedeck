#include "SettingsWindow.h"
#include "Autostart.h"

#include <commctrl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

struct TypeEntry {
    WidgetType type;
    const wchar_t* comboLabel;
    const wchar_t* listLabel;
};
constexpr TypeEntry kTypes[] = {
    {WidgetType::QuickActions, L"Quick Actions", L"Quick Actions"},
    {WidgetType::Media, L"Media (auto-detect)", L"Media"},
    {WidgetType::Brightness, L"Brightness (monitors)", L"Brightness"},
    {WidgetType::Lyrics, L"Lyrics (Spotify)", L"Lyrics"},
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
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 480, 400, nullptr, nullptr, hInstance,
                              this);
    if (!m_hwnd) return false;

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
    m_list = make(L"LISTBOX", nullptr, WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 12, 30, 180, 258,
                   kIdList);

    m_addButton = make(L"BUTTON", L"Add", BS_PUSHBUTTON, 12, 296, 56, 24, kIdAdd);
    m_removeButton = make(L"BUTTON", L"Remove", BS_PUSHBUTTON, 72, 296, 56, 24, kIdRemove);
    m_upButton = make(L"BUTTON", L"Up", BS_PUSHBUTTON, 132, 296, 34, 24, kIdUp);
    m_downButton = make(L"BUTTON", L"Down", BS_PUSHBUTTON, 168, 296, 42, 24, kIdDown);

    make(L"STATIC", L"Widget type:", 0, 210, 12, 140, 18, 0);
    m_typeCombo = make(L"COMBOBOX", nullptr, WS_BORDER | WS_VSCROLL | CBS_DROPDOWNLIST, 210, 30,
                        220, 200, kIdTypeCombo);
    for (const TypeEntry& entry : kTypes) {
        SendMessageW(m_typeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.comboLabel));
    }

    make(L"STATIC", L"Vertical position:", 0, 210, 64, 160, 18, 0);
    m_positionTrackbar =
        make(TRACKBAR_CLASSW, nullptr, WS_TABSTOP | TBS_HORZ, 210, 82, 180, 28, kIdTrackbar);
    SendMessageW(m_positionTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    m_positionLabel = make(L"STATIC", L"50%", 0, 396, 88, 44, 18, 0);

    make(L"STATIC", L"Tab width (px):", 0, 210, 118, 160, 18, 0);
    m_tabWidthEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 210, 136, 100, 22, kIdTabWidth);

    make(L"STATIC", L"Tab height (px):", 0, 210, 164, 160, 18, 0);
    m_tabHeightEdit =
        make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 210, 182, 100, 22, kIdTabHeight);

    make(L"STATIC", L"Panel width (px):", 0, 210, 210, 160, 18, 0);
    m_panelWidthEdit =
        make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 210, 228, 100, 22, kIdPanelWidth);

    m_autostartCheck = make(L"BUTTON", L"Start with Windows", BS_AUTOCHECKBOX | WS_TABSTOP, 210,
                             262, 220, 22, kIdAutostart);
    SendMessageW(m_autostartCheck, BM_SETCHECK,
                 Autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    m_saveButton = make(L"BUTTON", L"Save", BS_DEFPUSHBUTTON, 210, 296, 90, 26, kIdSave);
    m_closeButton = make(L"BUTTON", L"Close", BS_PUSHBUTTON, 308, 296, 90, 26, kIdClose);
}

void SettingsWindow::RefreshList(int selectIndex) {
    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const wchar_t* typeName = kTypes[TypeIndex(m_tabs[i].widgetType)].listLabel;
        wchar_t buf[64];
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
    bool hasSelection = m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_tabs.size());
    EnableWindow(m_typeCombo, hasSelection);
    EnableWindow(m_positionTrackbar, hasSelection);
    EnableWindow(m_tabWidthEdit, hasSelection);
    EnableWindow(m_tabHeightEdit, hasSelection);
    EnableWindow(m_panelWidthEdit, hasSelection);
    EnableWindow(m_removeButton, hasSelection);
    EnableWindow(m_upButton, hasSelection && m_selectedIndex > 0);
    EnableWindow(m_downButton,
                 hasSelection && m_selectedIndex < static_cast<int>(m_tabs.size()) - 1);

    if (!hasSelection) return;
    const TabSettings& t = m_tabs[m_selectedIndex];

    SendMessageW(m_typeCombo, CB_SETCURSEL, TypeIndex(t.widgetType), 0);

    int pos = static_cast<int>(std::lround(t.verticalRatio * 100.0f));
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

    int pos = static_cast<int>(SendMessageW(m_positionTrackbar, TBM_GETPOS, 0, 0));
    t.verticalRatio = std::clamp(pos, 0, 100) / 100.0f;

    wchar_t buf[32];
    GetWindowTextW(m_tabWidthEdit, buf, 32);
    t.tabWidth = std::clamp(static_cast<float>(_wtof(buf)), 12.0f, 60.0f);
    GetWindowTextW(m_tabHeightEdit, buf, 32);
    t.tabHeight = std::clamp(static_cast<float>(_wtof(buf)), 40.0f, 200.0f);
    GetWindowTextW(m_panelWidthEdit, buf, 32);
    t.panelWidth = std::clamp(static_cast<float>(_wtof(buf)), 200.0f, 480.0f);
}

void SettingsWindow::OnSelectionChanged() {
    int sel = static_cast<int>(SendMessageW(m_list, LB_GETCURSEL, 0, 0));
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
    int target = m_selectedIndex + delta;
    if (target < 0 || target >= static_cast<int>(m_tabs.size())) return;
    StoreControlsIntoSelected();
    std::swap(m_tabs[m_selectedIndex], m_tabs[target]);
    RefreshList(target);
}

void SettingsWindow::OnTrackbarChanged() {
    if (m_selectedIndex < 0) return;
    int pos = static_cast<int>(SendMessageW(m_positionTrackbar, TBM_GETPOS, 0, 0));
    wchar_t buf[16];
    swprintf_s(buf, L"%d%%", pos);
    SetWindowTextW(m_positionLabel, buf);
}

void SettingsWindow::OnSave() {
    StoreControlsIntoSelected();
    Autostart::SetEnabled(SendMessageW(m_autostartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (m_onSave) m_onSave(m_tabs);
    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            CreateControls(cs->hInstance);
            RefreshList(0);
            return 0;
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
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
            if (reinterpret_cast<HWND>(lParam) == m_positionTrackbar) {
                OnTrackbarChanged();
            }
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

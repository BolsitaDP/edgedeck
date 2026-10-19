#include "LayoutsWindow.h"

#include "AppIcon.h"
#include "Renderer.h"
#include "Strings.h"

#include <commctrl.h>
#include <shellscalingapi.h>

#include <algorithm>

namespace {
constexpr int kIdList = 400;
constexpr int kIdName = 401;
constexpr int kIdAdd = 402;
constexpr int kIdRemove = 403;
constexpr int kIdUp = 404;
constexpr int kIdDown = 405;
constexpr int kIdSave = 406;
constexpr int kIdClose = 407;
constexpr int kIdMonitorFirst = 420; // one checkbox per monitor from here

const wchar_t kClassName[] = L"EdgeDeckLayoutsWindow";

constexpr int kClientWidth = 484;
constexpr int kClientHeight = 372;
// Room for this many monitor boxes; more than that on one desk is not a case worth a scrollbar.
constexpr int kMaxMonitorRows = 6;

std::wstring TextOf(HWND edit) {
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(std::max(copied, 0)));
    return text;
}
} // namespace

bool LayoutsWindow::Create(HINSTANCE hInstance, std::vector<DisplayLayouts::Layout> initial, SaveCallback onSave) {
    m_layouts = std::move(initial);
    m_onSave = std::move(onSave);

    // The monitors: connected ones in the order Windows reports them, then any a layout names that
    // is not plugged in.
    m_connected = DisplayTopology::Enumerate();
    for (const DisplayTopology::Monitor& m : m_connected) {
        std::wstring label = m.name == m.id || m.name.empty() ? m.id : m.name + L" (" + m.id + L")";
        if (!m.active) label = Strings::Format(Strings::Id::SettingsMonitorOff, label.c_str());
        m_monitors.push_back({m.id, label});
        if (m.active) m_activeIds.push_back(m.id);
    }
    for (const DisplayLayouts::Layout& layout : m_layouts) {
        for (const std::wstring& id : layout.ids) {
            const bool known = std::any_of(m_monitors.begin(), m_monitors.end(),
                                           [&](const MonitorChoice& c) { return c.id == id; });
            if (!known) m_monitors.push_back({id, Strings::Format(Strings::Id::SettingsMonitorAbsent, id.c_str())});
        }
    }
    if (m_monitors.size() > kMaxMonitorRows) m_monitors.resize(kMaxMonitorRows);

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.hInstance = hInstance;
        wc.lpszClassName = kClassName;
        wc.lpfnWndProc = WndProc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.hIcon = AppIcon::Load(hInstance, false);
        wc.hIconSm = AppIcon::Load(hInstance, true);
        if (!RegisterClassExW(&wc)) return false;
        classRegistered = true;
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor)) cursor = POINT{0, 0};
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiX == 0) dpiX = 96;
    m_dpi = dpiX;

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT outer{0, 0, Scale(kClientWidth), Scale(kClientHeight)};
    AdjustWindowRectExForDpi(&outer, style, FALSE, exStyle, m_dpi);
    const int width = outer.right - outer.left;
    const int height = outer.bottom - outer.top;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfo(monitor, &mi)) {
        x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - width) / 2;
        y = std::max<int>(mi.rcWork.top, mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - height) / 2);
    }

    m_hwnd = CreateWindowExW(exStyle, kClassName, TR(LayoutsTitle), style, x, y, width, height, nullptr, nullptr,
                             hInstance, this);
    if (!m_hwnd) return false;
    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

HFONT LayoutsWindow::MakeFont() const {
    return CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void LayoutsWindow::ApplyDpi(UINT dpi) {
    if (dpi == 0 || dpi == m_dpi) return;
    m_dpi = dpi;
    HFONT previous = m_font;
    m_font = MakeFont();
    for (const Placement& p : m_placements) {
        const int left = Scale(p.x);
        const int top = Scale(p.y);
        SetWindowPos(p.hwnd, nullptr, left, top, Scale(p.x + p.w) - left, Scale(p.y + p.h) - top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(p.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    }
    if (previous) DeleteObject(previous);
}

void LayoutsWindow::CreateControls(HINSTANCE hInstance) {
    m_font = MakeFont();
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        const int left = Scale(x);
        const int top = Scale(y);
        HWND child = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, left, top, Scale(x + w) - left,
                                      Scale(y + h) - top, m_hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
        if (!child) return child;
        if (m_font) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        m_placements.push_back({child, x, y, w, h});
        return child;
    };

    // Left: the layouts, in shortcut order, and what arranges them.
    make(L"STATIC", TR(LayoutsList), 0, 12, 10, 200, 18, 0);
    m_list = make(L"LISTBOX", nullptr, WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, 12, 30, 200, 216, kIdList);
    m_addButton = make(L"BUTTON", TR(LayoutsAddCurrent), BS_PUSHBUTTON | WS_TABSTOP, 12, 254, 120, 24, kIdAdd);
    m_removeButton = make(L"BUTTON", TR(ButtonRemove), BS_PUSHBUTTON | WS_TABSTOP, 136, 254, 76, 24, kIdRemove);
    m_upButton = make(L"BUTTON", TR(ButtonUp), BS_PUSHBUTTON | WS_TABSTOP, 12, 284, 60, 24, kIdUp);
    m_downButton = make(L"BUTTON", TR(ButtonDown), BS_PUSHBUTTON | WS_TABSTOP, 76, 284, 60, 24, kIdDown);

    // Right: the selected layout.
    make(L"STATIC", TR(ActionsName), 0, 238, 12, 234, 18, 0);
    m_nameEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 238, 30, 234, 22, kIdName);
    SendMessageW(m_nameEdit, EM_SETLIMITTEXT, DisplayLayouts::kMaxNameLength, 0);
    make(L"STATIC", TR(LayoutsMonitors), 0, 238, 62, 234, 18, 0);
    for (size_t i = 0; i < m_monitors.size(); ++i) {
        m_monitorChecks.push_back(make(L"BUTTON", m_monitors[i].label.c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, 238,
                                       82 + static_cast<int>(i) * 24, 234, 22, kIdMonitorFirst + static_cast<int>(i)));
    }
    make(L"STATIC", TR(LayoutsHelp), 0, 238, 232, 234, 90, 0);

    m_status = make(L"STATIC", L"", 0, 12, 326, 298, 40, 0);
    make(L"BUTTON", TR(ButtonSave), BS_DEFPUSHBUTTON | WS_TABSTOP, 318, 334, 76, 26, kIdSave);
    make(L"BUTTON", TR(ButtonClose), BS_PUSHBUTTON | WS_TABSTOP, 398, 334, 74, 26, kIdClose);
}

void LayoutsWindow::SetStatus(const wchar_t* text) { SetWindowTextW(m_status, text); }

void LayoutsWindow::RefreshList(int selectIndex) {
    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < m_layouts.size(); ++i) {
        // Numbered: the number is the shortcut's digit, and the badge the panel shows.
        const std::wstring entry = Strings::Format(Strings::Id::SettingsListEntry, i + 1,
                                                   m_layouts[i].name.empty() ? TR(NoName) : m_layouts[i].name.c_str());
        SendMessageW(m_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.c_str()));
    }
    if (m_layouts.empty()) {
        m_selected = -1;
    } else {
        m_selected = std::clamp(selectIndex, 0, static_cast<int>(m_layouts.size()) - 1);
        SendMessageW(m_list, LB_SETCURSEL, static_cast<WPARAM>(m_selected), 0);
    }
    LoadSelectedIntoControls();
}

void LayoutsWindow::LoadSelectedIntoControls() {
    const int count = static_cast<int>(m_layouts.size());
    const bool has = m_selected >= 0 && m_selected < count;
    EnableWindow(m_nameEdit, has);
    EnableWindow(m_removeButton, has);
    EnableWindow(m_upButton, has && m_selected > 0);
    EnableWindow(m_downButton, has && m_selected < count - 1);
    EnableWindow(m_addButton, m_layouts.size() < DisplayLayouts::kMaxLayouts && !m_activeIds.empty());

    m_loading = true; // filling the controls in must not look like the user editing them
    SetWindowTextW(m_nameEdit, has ? m_layouts[static_cast<size_t>(m_selected)].name.c_str() : L"");
    for (size_t i = 0; i < m_monitorChecks.size(); ++i) {
        bool on = false;
        if (has) {
            const auto& ids = m_layouts[static_cast<size_t>(m_selected)].ids;
            on = std::find(ids.begin(), ids.end(), m_monitors[i].id) != ids.end();
        }
        EnableWindow(m_monitorChecks[i], has);
        SendMessageW(m_monitorChecks[i], BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    m_loading = false;
}

void LayoutsWindow::StoreControlsIntoSelected() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_layouts.size())) return;
    DisplayLayouts::Layout& layout = m_layouts[static_cast<size_t>(m_selected)];
    layout.name = TextOf(m_nameEdit);
    layout.ids.clear();
    for (size_t i = 0; i < m_monitorChecks.size(); ++i) {
        if (SendMessageW(m_monitorChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED) layout.ids.push_back(m_monitors[i].id);
    }
}

void LayoutsWindow::OnNameEdited() {
    if (m_loading || m_selected < 0 || m_selected >= static_cast<int>(m_layouts.size())) return;
    m_layouts[static_cast<size_t>(m_selected)].name = TextOf(m_nameEdit);
    const std::wstring& name = m_layouts[static_cast<size_t>(m_selected)].name;
    const std::wstring entry = Strings::Format(Strings::Id::SettingsListEntry, static_cast<size_t>(m_selected) + 1,
                                               name.empty() ? TR(NoName) : name.c_str());
    SendMessageW(m_list, LB_DELETESTRING, static_cast<WPARAM>(m_selected), 0);
    SendMessageW(m_list, LB_INSERTSTRING, static_cast<WPARAM>(m_selected), reinterpret_cast<LPARAM>(entry.c_str()));
    SendMessageW(m_list, LB_SETCURSEL, static_cast<WPARAM>(m_selected), 0);
}

void LayoutsWindow::OnAddCurrent() {
    if (m_layouts.size() >= DisplayLayouts::kMaxLayouts) {
        SetStatus(Strings::Format(Strings::Id::LayoutsFull, DisplayLayouts::kMaxLayouts).c_str());
        return;
    }
    StoreControlsIntoSelected();
    m_layouts.push_back({DisplayLayouts::NameFor(m_activeIds, m_connected), m_activeIds});
    SetStatus(L"");
    RefreshList(static_cast<int>(m_layouts.size()) - 1);
    SetFocus(m_nameEdit);
    SendMessageW(m_nameEdit, EM_SETSEL, 0, -1);
}

void LayoutsWindow::OnRemove() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_layouts.size())) return;
    m_layouts.erase(m_layouts.begin() + m_selected);
    SetStatus(L"");
    RefreshList(m_selected);
}

void LayoutsWindow::OnMove(int delta) {
    if (m_selected < 0) return;
    const int target = m_selected + delta;
    if (target < 0 || target >= static_cast<int>(m_layouts.size())) return;
    StoreControlsIntoSelected();
    std::swap(m_layouts[static_cast<size_t>(m_selected)], m_layouts[static_cast<size_t>(target)]);
    RefreshList(target);
}

void LayoutsWindow::OnSave() {
    StoreControlsIntoSelected();
    // A layout needs a name and a monitor. Rather than drop one quietly, point at it and say so.
    for (size_t i = 0; i < m_layouts.size(); ++i) {
        const DisplayLayouts::Layout& l = m_layouts[i];
        const bool noName = DisplayLayouts::Sanitize({{l.name, {L"X"}}}).empty();
        const bool noMonitor = l.ids.empty();
        if (!noName && !noMonitor) continue;
        m_selected = static_cast<int>(i);
        SendMessageW(m_list, LB_SETCURSEL, i, 0);
        LoadSelectedIntoControls();
        SetStatus(noName ? TR(LayoutsNeedName) : TR(LayoutsNeedMonitor));
        if (noName) SetFocus(m_nameEdit);
        return;
    }
    if (m_onSave) m_onSave(m_layouts);
    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK LayoutsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<LayoutsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<LayoutsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT LayoutsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            m_hwnd = hwnd;
            if (const UINT dpi = GetDpiForWindow(hwnd)) m_dpi = dpi;
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            CreateControls(cs->hInstance);
            RefreshList(0);
            m_theme.Apply(hwnd);
            return 0;
        }
        case WM_ERASEBKGND:
            if (m_theme.EraseBackground(hwnd, reinterpret_cast<HDC>(wParam))) return 1;
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            if (m_theme.Themed()) {
                const HWND control = reinterpret_cast<HWND>(lParam);
                return m_theme.ControlColor(msg, reinterpret_cast<HDC>(wParam), control,
                                            GetDlgCtrlID(control) == kIdName);
            }
            break;
        case WM_DPICHANGED: {
            ApplyDpi(HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                             suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            PanelTheme::Refresh();
            m_theme.Apply(hwnd);
            break;
        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);
            if (id == kIdList && code == LBN_SELCHANGE) {
                StoreControlsIntoSelected();
                const int sel = static_cast<int>(SendMessageW(m_list, LB_GETCURSEL, 0, 0));
                if (sel != LB_ERR) {
                    m_selected = sel;
                    SetStatus(L"");
                    LoadSelectedIntoControls();
                }
            } else if (id == kIdName && code == EN_CHANGE) {
                OnNameEdited();
            } else if (id >= kIdMonitorFirst && id < kIdMonitorFirst + kMaxMonitorRows) {
                if (!m_loading) {
                    StoreControlsIntoSelected();
                    SetStatus(L"");
                }
            } else if (id == kIdAdd) {
                OnAddCurrent();
            } else if (id == kIdRemove) {
                OnRemove();
            } else if (id == kIdUp) {
                OnMove(-1);
            } else if (id == kIdDown) {
                OnMove(1);
            } else if (id == kIdSave) {
                OnSave();
            } else if (id == kIdClose || id == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (m_font) {
                DeleteObject(m_font);
                m_font = nullptr;
            }
            m_theme.Release();
            m_placements.clear();
            m_monitorChecks.clear();
            m_hwnd = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

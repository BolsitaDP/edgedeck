#include "ActionsWindow.h"

#include "AppIcon.h"
#include "Renderer.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cwchar>

namespace {
constexpr int kIdList = 200;
constexpr int kIdName = 201;
constexpr int kIdTarget = 202;
constexpr int kIdArgs = 203;
constexpr int kIdBrowse = 204;
constexpr int kIdAdd = 205;
constexpr int kIdRemove = 206;
constexpr int kIdUp = 207;
constexpr int kIdDown = 208;
constexpr int kIdSave = 209;
constexpr int kIdClose = 210;

const wchar_t kClassName[] = L"EdgeDeckActionsWindow";

// The client area in logical (96-DPI) units: what the controls are laid out in.
constexpr int kClientWidth = 484;
constexpr int kClientHeight = 352;

// What a freshly added row says, until the user types a name or picks a file.
const wchar_t kNewActionName[] = L"New action";
} // namespace

bool ActionsWindow::Create(HINSTANCE hInstance, std::vector<QuickAction> initial, SaveCallback onSave) {
    m_actions = std::move(initial);
    m_onSave = std::move(onSave);

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

    // On the monitor the pointer is on, created right there so the window takes that monitor's
    // scale from the start (a window made elsewhere and moved keeps the DPI it was born with).
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
        y = std::max<int>(mi.rcWork.top,
                          mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - height) / 2);
    }

    m_hwnd = CreateWindowExW(exStyle, kClassName, L"EdgeDeck Quick Actions", style, x, y, width, height,
                             nullptr, nullptr, hInstance, this);
    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

HFONT ActionsWindow::MakeFont() const {
    return CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void ActionsWindow::ApplyDpi(UINT dpi) {
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

void ActionsWindow::CreateControls(HINSTANCE hInstance) {
    m_font = MakeFont();

    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
                    int id) {
        const int left = Scale(x);
        const int top = Scale(y);
        HWND child = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, left, top,
                                      Scale(x + w) - left, Scale(y + h) - top, m_hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance,
                                      nullptr);
        if (!child) return child;
        if (m_font) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        m_placements.push_back({child, x, y, w, h});
        return child;
    };

    make(L"STATIC", L"Actions", 0, 12, 10, 100, 18, 0);
    m_list = make(L"LISTBOX", nullptr, WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP, 12, 30, 200,
                  236, kIdList);
    m_addButton = make(L"BUTTON", L"Add", BS_PUSHBUTTON, 12, 274, 56, 24, kIdAdd);
    m_removeButton = make(L"BUTTON", L"Remove", BS_PUSHBUTTON, 72, 274, 66, 24, kIdRemove);
    m_upButton = make(L"BUTTON", L"Up", BS_PUSHBUTTON, 142, 274, 34, 24, kIdUp);
    m_downButton = make(L"BUTTON", L"Down", BS_PUSHBUTTON, 180, 274, 50, 24, kIdDown);

    // The right-hand column starts clear of the Down button, which ends at x = 230.
    make(L"STATIC", L"Name:", 0, 238, 12, 100, 18, 0);
    m_nameEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 238, 30, 234, 22, kIdName);
    SendMessageW(m_nameEdit, EM_SETLIMITTEXT, QuickActions::kMaxNameLength, 0);

    make(L"STATIC", L"Opens:", 0, 238, 62, 100, 18, 0);
    m_targetEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 238, 80, 150, 22,
                        kIdTarget);
    SendMessageW(m_targetEdit, EM_SETLIMITTEXT, QuickActions::kMaxTargetLength, 0);
    m_browseButton = make(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 394, 79, 78, 24, kIdBrowse);

    make(L"STATIC", L"Arguments (optional):", 0, 238, 112, 200, 18, 0);
    m_argsEdit = make(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 238, 130, 234, 22, kIdArgs);
    SendMessageW(m_argsEdit, EM_SETLIMITTEXT, QuickActions::kMaxArgumentsLength, 0);

    make(L"STATIC",
         L"What it opens can be a program, a document, a folder or a web address (https://...). "
         L"Windows variables such as %ProgramFiles% are filled in. The row shows the name.",
         0, 238, 164, 234, 126, 0);

    m_status = make(L"STATIC", L"", 0, 12, 308, 304, 36, 0);
    make(L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 326, 312, 70, 26, kIdSave);
    make(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 402, 312, 70, 26, kIdClose);
}

void ActionsWindow::SetStatus(const wchar_t* text) { SetWindowTextW(m_status, text); }

void ActionsWindow::RefreshList(int selectIndex) {
    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    for (const QuickAction& action : m_actions) {
        SendMessageW(m_list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(action.name.empty() ? L"(no name)" : action.name.c_str()));
    }
    if (m_actions.empty()) {
        m_selected = -1;
    } else {
        m_selected = std::clamp(selectIndex, 0, static_cast<int>(m_actions.size()) - 1);
        SendMessageW(m_list, LB_SETCURSEL, static_cast<WPARAM>(m_selected), 0);
    }
    LoadSelectedIntoControls();
}

void ActionsWindow::LoadSelectedIntoControls() {
    const bool has = m_selected >= 0 && m_selected < static_cast<int>(m_actions.size());
    const int count = static_cast<int>(m_actions.size());
    EnableWindow(m_nameEdit, has);
    EnableWindow(m_targetEdit, has);
    EnableWindow(m_argsEdit, has);
    EnableWindow(m_browseButton, has);
    EnableWindow(m_removeButton, has);
    EnableWindow(m_upButton, has && m_selected > 0);
    EnableWindow(m_downButton, has && m_selected < count - 1);
    EnableWindow(m_addButton, static_cast<size_t>(count) < QuickActions::kMaxActions);

    m_loading = true; // setting the text below must not look like the user editing it
    if (has) {
        const QuickAction& a = m_actions[static_cast<size_t>(m_selected)];
        SetWindowTextW(m_nameEdit, a.name.c_str());
        SetWindowTextW(m_targetEdit, a.target.c_str());
        SetWindowTextW(m_argsEdit, a.arguments.c_str());
    } else {
        SetWindowTextW(m_nameEdit, L"");
        SetWindowTextW(m_targetEdit, L"");
        SetWindowTextW(m_argsEdit, L"");
    }
    m_loading = false;
}

static std::wstring TextOf(HWND edit) {
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(std::max(copied, 0)));
    return text;
}

void ActionsWindow::StoreControlsIntoSelected() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_actions.size())) return;
    QuickAction& a = m_actions[static_cast<size_t>(m_selected)];
    a.name = TextOf(m_nameEdit);
    a.target = TextOf(m_targetEdit);
    a.arguments = TextOf(m_argsEdit);
}

void ActionsWindow::OnNameEdited() {
    if (m_loading || m_selected < 0 || m_selected >= static_cast<int>(m_actions.size())) return;
    // The list shows the name as it is typed. Replacing the one entry keeps the selection and sends
    // no notification, so this does not loop back.
    m_actions[static_cast<size_t>(m_selected)].name = TextOf(m_nameEdit);
    const std::wstring& name = m_actions[static_cast<size_t>(m_selected)].name;
    SendMessageW(m_list, LB_DELETESTRING, static_cast<WPARAM>(m_selected), 0);
    SendMessageW(m_list, LB_INSERTSTRING, static_cast<WPARAM>(m_selected),
                 reinterpret_cast<LPARAM>(name.empty() ? L"(no name)" : name.c_str()));
    SendMessageW(m_list, LB_SETCURSEL, static_cast<WPARAM>(m_selected), 0);
}

void ActionsWindow::OnAdd() {
    if (m_actions.size() >= QuickActions::kMaxActions) return;
    StoreControlsIntoSelected();
    QuickAction a;
    a.name = kNewActionName;
    m_actions.push_back(std::move(a));
    SetStatus(L"");
    RefreshList(static_cast<int>(m_actions.size()) - 1);
    SetFocus(m_nameEdit);
    SendMessageW(m_nameEdit, EM_SETSEL, 0, -1);
}

void ActionsWindow::OnRemove() {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_actions.size())) return;
    m_actions.erase(m_actions.begin() + m_selected);
    SetStatus(L"");
    RefreshList(m_selected);
}

void ActionsWindow::OnMove(int delta) {
    if (m_selected < 0) return;
    const int target = m_selected + delta;
    if (target < 0 || target >= static_cast<int>(m_actions.size())) return;
    StoreControlsIntoSelected();
    std::swap(m_actions[static_cast<size_t>(m_selected)], m_actions[static_cast<size_t>(target)]);
    RefreshList(target);
}

void ActionsWindow::OnBrowse() {
    if (m_selected < 0) return;
    wchar_t path[MAX_PATH * 2] = {};
    wcsncpy_s(path, TextOf(m_targetEdit).c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Programs and documents\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    ofn.lpstrTitle = L"Choose what this action opens";
    // Not NOCHANGEDIR's absence: the dialog must not move the process's working directory.
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return; // cancelled

    SetWindowTextW(m_targetEdit, path);
    // A row still called "New action" takes the file's name, which is nearly always what was meant.
    const std::wstring current = TextOf(m_nameEdit);
    if (current.empty() || current == kNewActionName) {
        std::wstring name = path;
        const size_t slash = name.find_last_of(L"\\/");
        if (slash != std::wstring::npos) name = name.substr(slash + 1);
        const size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) name.resize(dot);
        SetWindowTextW(m_nameEdit, name.c_str()); // fires OnNameEdited, which updates the list
    }
}

void ActionsWindow::OnSave() {
    StoreControlsIntoSelected();

    // A row needs a name and something to open. Rather than quietly leaving one out - the user
    // would see the list shrink and not know why - stay here, point at it and say so, in the
    // window and not in a dialog.
    for (size_t i = 0; i < m_actions.size(); ++i) {
        const QuickAction& a = m_actions[i];
        const bool noName = QuickActions::Sanitize({QuickAction{a.name, L"x", L""}}).empty();
        const bool noTarget = QuickActions::Sanitize({QuickAction{L"x", a.target, L""}}).empty();
        if (!noName && !noTarget) continue;

        SendMessageW(m_list, LB_SETCURSEL, i, 0);
        m_selected = static_cast<int>(i);
        LoadSelectedIntoControls();
        SetStatus(noName ? L"Give this action a name, or remove it."
                         : L"Say what this action opens (use Browse...), or remove it.");
        SetFocus(noName ? m_nameEdit : m_targetEdit);
        return;
    }

    if (m_onSave) m_onSave(m_actions);
    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK ActionsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ActionsWindow* self = reinterpret_cast<ActionsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ActionsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ActionsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
                const int id = GetDlgCtrlID(control);
                // A disabled edit box paints as a STATIC; the id says it is a field.
                const bool field = id == kIdName || id == kIdTarget || id == kIdArgs;
                return m_theme.ControlColor(msg, reinterpret_cast<HDC>(wParam), control, field);
            }
            break;
        case WM_DPICHANGED: {
            ApplyDpi(HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
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
            } else if ((id == kIdTarget || id == kIdArgs) && code == EN_CHANGE) {
                if (!m_loading) SetStatus(L"");
            } else if (id == kIdAdd) {
                OnAdd();
            } else if (id == kIdRemove) {
                OnRemove();
            } else if (id == kIdUp) {
                OnMove(-1);
            } else if (id == kIdDown) {
                OnMove(1);
            } else if (id == kIdBrowse) {
                OnBrowse();
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
            m_hwnd = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

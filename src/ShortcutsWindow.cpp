#include "ShortcutsWindow.h"

#include "AppIcon.h"
#include "Renderer.h"
#include "Strings.h"

#include <commctrl.h>
#include <shellscalingapi.h>

#include <algorithm>

namespace {
constexpr int kIdExit = 300;
constexpr int kIdPause = 301;
constexpr int kIdTabs = 302;
constexpr int kIdDisplays = 303;
constexpr int kIdSave = 304;
constexpr int kIdClose = 305;

const wchar_t kClassName[] = L"EdgeDeckShortcutsWindow";

constexpr int kClientWidth = 460;
constexpr int kClientHeight = 286;

// The modifier choices for a family of digits, in the order the list shows them. Each includes
// Ctrl, Alt or Win (Shortcuts::HasRequiredModifier); 0 is "off".
constexpr UINT kFamilyChoices[] = {
    MOD_CONTROL | MOD_ALT,
    MOD_CONTROL | MOD_ALT | MOD_SHIFT,
    MOD_CONTROL | MOD_SHIFT,
    MOD_ALT | MOD_SHIFT,
    MOD_WIN | MOD_ALT,
    MOD_WIN | MOD_CONTROL,
    0,
};
constexpr int kFamilyChoiceCount = static_cast<int>(sizeof(kFamilyChoices) / sizeof(kFamilyChoices[0]));

// Windows' hotkey control speaks HOTKEYF_* in the high byte of its value; RegisterHotKey speaks MOD_*.
Shortcut FromHotkeyControl(HWND control) {
    const WORD value = static_cast<WORD>(SendMessageW(control, HKM_GETHOTKEY, 0, 0));
    const UINT key = LOBYTE(value);
    const UINT flags = HIBYTE(value);
    if (key == 0) return Shortcut{};
    UINT modifiers = 0;
    if (flags & HOTKEYF_CONTROL) modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT) modifiers |= MOD_ALT;
    if (flags & HOTKEYF_SHIFT) modifiers |= MOD_SHIFT;
    return Shortcut{modifiers, key};
}

void ToHotkeyControl(HWND control, const Shortcut& shortcut) {
    BYTE flags = 0;
    if (shortcut.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (shortcut.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    if (shortcut.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    SendMessageW(control, HKM_SETHOTKEY, MAKEWORD(static_cast<BYTE>(shortcut.key), flags), 0);
}
} // namespace

bool ShortcutsWindow::Create(HINSTANCE hInstance, Shortcuts::Set initial, SaveCallback onSave) {
    m_initial = initial;
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

    // On the monitor the pointer is on, created there so it starts at that monitor's scale.
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

    m_hwnd = CreateWindowExW(exStyle, kClassName, TR(ShortcutsTitle), style, x, y, width, height, nullptr,
                             nullptr, hInstance, this);
    if (!m_hwnd) return false;
    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

HFONT ShortcutsWindow::MakeFont() const { return DialogTheme::MakeFont(m_dpi, false); }

// The hotkey box has no visual style to darken, so in the dark look it is painted here: its field
// colour, the shortcut in EdgeDeck's own words ("Ctrl+Alt+Mayús+Q"), and an accent line along the
// bottom while it has the keyboard. That line stands in for the caret, which is removed: it would
// sit where the control's own wording of the keys ends, not ours. In the light look the control
// paints itself.
LRESULT CALLBACK ShortcutsWindow::HotkeyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                             DWORD_PTR ref) {
    const auto* self = reinterpret_cast<const ShortcutsWindow*>(ref);
    const bool dark = self && self->m_theme.Themed();
    switch (msg) {
        case WM_PAINT: {
            if (!dark) break;
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT box{};
            GetClientRect(hwnd, &box);
            HBRUSH field = CreateSolidBrush(self->m_theme.FieldColor());
            FillRect(dc, &box, field);
            DeleteObject(field);

            const WORD value = static_cast<WORD>(SendMessageW(hwnd, HKM_GETHOTKEY, 0, 0));
            const Shortcut shortcut = FromHotkeyControl(hwnd);
            UINT typed = 0; // modifiers held while the key is still to come
            if (HIBYTE(value) & HOTKEYF_CONTROL) typed |= MOD_CONTROL;
            if (HIBYTE(value) & HOTKEYF_ALT) typed |= MOD_ALT;
            if (HIBYTE(value) & HOTKEYF_SHIFT) typed |= MOD_SHIFT;
            const std::wstring text =
                !shortcut.Off() ? Shortcuts::Display(shortcut)
                : typed         ? Shortcuts::DisplayModifiers(typed)
                                : Shortcuts::Display(Shortcut{});
            HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, self->m_theme.TextColor());
            const UINT dpi = GetDpiForWindow(hwnd);
            RECT textBox = box;
            textBox.left += MulDiv(5, static_cast<int>(dpi), 96);
            DrawTextW(dc, text.c_str(), -1, &textBox, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
            SelectObject(dc, oldFont);

            if (GetFocus() == hwnd) {
                RECT line = box;
                line.top = line.bottom - std::max(2, MulDiv(2, static_cast<int>(dpi), 96));
                HBRUSH accent = CreateSolidBrush(self->m_theme.AccentColor());
                FillRect(dc, &line, accent);
                DeleteObject(accent);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            if (dark) return 1;
            break;
        case WM_NCPAINT: {
            if (!dark) break;
            // The one-pixel border WS_BORDER gives it, in the outline colour instead of white.
            HDC dc = GetWindowDC(hwnd);
            RECT frame{};
            GetWindowRect(hwnd, &frame);
            OffsetRect(&frame, -frame.left, -frame.top);
            HBRUSH outline = CreateSolidBrush(self->m_theme.OutlineColor());
            FrameRect(dc, &frame, outline);
            DeleteObject(outline);
            ReleaseDC(hwnd, dc);
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            if (dark) {
                if (msg == WM_SETFOCUS) DestroyCaret();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, HotkeyProc, 1);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void ShortcutsWindow::ApplyDpi(UINT dpi) {
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

void ShortcutsWindow::CreateControls(HINSTANCE hInstance) {
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

    // Two columns: what the shortcut does, and its keys.
    make(L"STATIC", TR(ShortcutExitLabel), 0, 16, 20, 176, 20, 0);
    m_exitHotkey = make(HOTKEY_CLASSW, nullptr, WS_BORDER | WS_TABSTOP, 200, 16, 244, 28, kIdExit);
    make(L"STATIC", TR(ShortcutPauseLabel), 0, 16, 56, 176, 20, 0);
    m_pauseHotkey = make(HOTKEY_CLASSW, nullptr, WS_BORDER | WS_TABSTOP, 200, 52, 244, 28, kIdPause);

    // Without at least Ctrl or Alt the control would accept a bare letter; it is given those
    // instead, which is also what Problem() would demand. In the dark look each box is painted by
    // HotkeyProc.
    for (HWND control : {m_exitHotkey, m_pauseHotkey}) {
        SendMessageW(control, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S, MAKELPARAM(HOTKEYF_CONTROL | HOTKEYF_ALT, 0));
        SetWindowSubclass(control, HotkeyProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }

    make(L"STATIC", TR(ShortcutTabsLabel), 0, 16, 92, 176, 20, 0);
    m_tabsCombo = make(L"COMBOBOX", nullptr, WS_BORDER | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP, 200, 88, 244,
                       200, kIdTabs);
    make(L"STATIC", TR(ShortcutDisplaysLabel), 0, 16, 128, 176, 20, 0);
    m_displaysCombo = make(L"COMBOBOX", nullptr, WS_BORDER | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP, 200, 124,
                           244, 200, kIdDisplays);
    for (int i = 0; i < kFamilyChoiceCount; ++i) {
        const std::wstring tabs = kFamilyChoices[i] ? Shortcuts::DisplayFamily(kFamilyChoices[i], Shortcuts::kTabCount)
                                                    : std::wstring(TR(ShortcutOff));
        const std::wstring displays = kFamilyChoices[i]
                                          ? Shortcuts::DisplayFamily(kFamilyChoices[i], Shortcuts::kDisplayCount)
                                          : std::wstring(TR(ShortcutOff));
        SendMessageW(m_tabsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tabs.c_str()));
        SendMessageW(m_displaysCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(displays.c_str()));
    }

    make(L"STATIC", TR(ShortcutsHelp), 0, 16, 168, 428, 56, DialogTheme::kSecondaryIdFirst);

    // Footer: what could not be done, if anything, and Save / Close, under a line.
    make(L"STATIC", L"", 0, 16, 234, 428, 1, DialogTheme::kDividerIdFirst);
    m_status = make(L"STATIC", L"", 0, 16, 244, 236, 38, 0);
    make(L"BUTTON", TR(ButtonSave), BS_DEFPUSHBUTTON | WS_TABSTOP, 268, 246, 84, 30, kIdSave);
    make(L"BUTTON", TR(ButtonClose), BS_PUSHBUTTON | WS_TABSTOP, 360, 246, 84, 30, kIdClose);
}
void ShortcutsWindow::LoadIntoControls() {
    ToHotkeyControl(m_exitHotkey, m_initial.exit);
    ToHotkeyControl(m_pauseHotkey, m_initial.pause);
    auto select = [](HWND combo, UINT modifiers, int count) {
        for (int i = 0; i < kFamilyChoiceCount; ++i) {
            if (kFamilyChoices[i] == modifiers) {
                SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
                return;
            }
        }
        // A combination typed into config.txt that the list does not offer is added to it, so
        // opening this window and pressing Save does not quietly change it.
        const std::wstring text = Shortcuts::DisplayFamily(modifiers, count);
        const LRESULT at = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(at), static_cast<LPARAM>(modifiers));
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(at), 0);
    };
    select(m_tabsCombo, m_initial.tabModifiers, Shortcuts::kTabCount);
    select(m_displaysCombo, m_initial.displayModifiers, Shortcuts::kDisplayCount);
}

Shortcuts::Set ShortcutsWindow::ReadControls() const {
    Shortcuts::Set set = m_initial;
    if (m_exitEdited) set.exit = FromHotkeyControl(m_exitHotkey);
    if (m_pauseEdited) set.pause = FromHotkeyControl(m_pauseHotkey);
    auto family = [](HWND combo) -> UINT {
        const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (index == CB_ERR) return 0;
        if (index < kFamilyChoiceCount) return kFamilyChoices[index];
        return static_cast<UINT>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0));
    };
    set.tabModifiers = family(m_tabsCombo);
    set.displayModifiers = family(m_displaysCombo);
    return set;
}

void ShortcutsWindow::SetStatus(const wchar_t* text) { SetWindowTextW(m_status, text); }

void ShortcutsWindow::OnSave() {
    const Shortcuts::Set set = ReadControls();
    // Said here, before anything is kept: a set that cannot work is not saved.
    const std::wstring problem = Shortcuts::Problem(set);
    if (!problem.empty()) {
        SetStatus(problem.c_str());
        return;
    }
    const std::wstring taken = m_onSave ? m_onSave(set) : std::wstring();
    if (!taken.empty()) {
        // Kept and registered as far as it could be; the window stays so the rest can be changed.
        m_initial = set;
        m_exitEdited = m_pauseEdited = false;
        SetStatus(taken.c_str());
        return;
    }
    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK ShortcutsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ShortcutsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ShortcutsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ShortcutsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            m_hwnd = hwnd;
            if (const UINT dpi = GetDpiForWindow(hwnd)) m_dpi = dpi;
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            CreateControls(cs->hInstance);
            LoadIntoControls();
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
            if (msg == WM_CTLCOLORSTATIC) {
                if (const LRESULT special =
                        m_theme.SpecialStatic(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam))) {
                    return special;
                }
            }
            if (m_theme.Themed()) {
                return m_theme.ControlColor(msg, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam), false);
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
            InvalidateRect(m_exitHotkey, nullptr, TRUE);
            InvalidateRect(m_pauseHotkey, nullptr, TRUE);
            break;
        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);
            if (id == kIdExit && code == EN_CHANGE) {
                m_exitEdited = true;
                SetStatus(L"");
            } else if (id == kIdPause && code == EN_CHANGE) {
                m_pauseEdited = true;
                SetStatus(L"");
            } else if ((id == kIdTabs || id == kIdDisplays) && code == CBN_SELCHANGE) {
                SetStatus(L"");
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

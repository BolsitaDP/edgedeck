#include "EdgeWindow.h"
#include "Actions.h"

#include <shellscalingapi.h>
#include <windowsx.h>
#include <cmath>
#include <algorithm>

namespace {
const wchar_t kTabClassName[] = L"EdgeDeckTabWindow";
const wchar_t kPanelClassName[] = L"EdgeDeckPanelWindow";

void ApplyRoundedRegion(HWND hwnd, int widthPx, int heightPx, float radiusPx) {
    // CreateRoundRectRgn's last two params are the rounding ellipse's
    // width/height (diameter), not a radius - double it to match radiusPx.
    int diameter = static_cast<int>(std::lround(radiusPx * 2.0f));
    HRGN region = CreateRoundRectRgn(0, 0, widthPx + 1, heightPx + 1, diameter, diameter);
    SetWindowRgn(hwnd, region, TRUE); // ownership transferred to the window
}
} // namespace

EdgeWindow* EdgeWindow::s_instance = nullptr;

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

bool EdgeWindow::Create(HINSTANCE hInstance) {
    s_instance = this;

    m_items = {
        {L"Open Notepad"},
        {L"Open Calculator"},
        {L"Show Desktop"},
    };
    m_config.panelHeight = PanelLayout::TitleHeight +
                            static_cast<float>(m_items.size()) * PanelLayout::RowHeight +
                            PanelLayout::BottomPadding;

    RegisterClasses(hInstance);
    ComputeLayout();
    CreateWindows(hInstance);

    return m_tabHwnd != nullptr && m_panelHwnd != nullptr;
}

void EdgeWindow::RegisterClasses(HINSTANCE hInstance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint the whole client area ourselves

    wc.lpszClassName = kTabClassName;
    wc.lpfnWndProc = TabProc;
    RegisterClassExW(&wc);

    wc.lpszClassName = kPanelClassName;
    wc.lpfnWndProc = PanelProc;
    RegisterClassExW(&wc);
}

void EdgeWindow::ComputeLayout() {
    POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfo(monitor, &mi);

    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    m_dpi = dpiX;
    m_dpiScale = static_cast<float>(dpiX) / 96.0f;

    const int monRight = mi.rcMonitor.right;
    const int monTop = mi.rcMonitor.top;
    const int monBottom = mi.rcMonitor.bottom;
    const int monHeight = monBottom - monTop;

    const int tabWpx = static_cast<int>(std::lround(m_config.tabWidth * m_dpiScale));
    const int tabHpx = static_cast<int>(std::lround(m_config.tabHeight * m_dpiScale));
    m_panelWidthPx = static_cast<int>(std::lround(m_config.panelWidth * m_dpiScale));
    m_panelHeightPx = static_cast<int>(std::lround(m_config.panelHeight * m_dpiScale));

    // Right-edge placement (the only edge this MVP supports; see Edge enum).
    const int tabX = monRight - tabWpx;
    const int tabY = monTop + static_cast<int>((monHeight - tabHpx) * m_config.verticalRatio);
    m_tabRectPx = {tabX, tabY, tabX + tabWpx, tabY + tabHpx};

    m_panelYPx = tabY + (tabHpx - m_panelHeightPx) / 2;
    m_panelYPx = std::clamp(m_panelYPx, monTop, monBottom - m_panelHeightPx);

    m_panelOpenXPx = tabX - m_panelWidthPx; // resting position, left of the tab
    m_panelClosedXPx = tabX;                // tucked back under the tab, off-screen edge ok
}

void EdgeWindow::CreateWindows(HINSTANCE hInstance) {
    const DWORD exStyle =
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;

    m_tabHwnd = CreateWindowExW(
        exStyle, kTabClassName, L"EdgeDeck", WS_POPUP, m_tabRectPx.left, m_tabRectPx.top,
        m_tabRectPx.right - m_tabRectPx.left, m_tabRectPx.bottom - m_tabRectPx.top, nullptr,
        nullptr, hInstance, nullptr);
    if (!m_tabHwnd) return;

    SetLayeredWindowAttributes(m_tabHwnd, 0, m_config.windowAlpha, LWA_ALPHA);
    ApplyRoundedRegion(m_tabHwnd, m_tabRectPx.right - m_tabRectPx.left,
                        m_tabRectPx.bottom - m_tabRectPx.top, m_config.cornerRadius * m_dpiScale);
    m_tabRenderer.AttachToWindow(m_tabHwnd);
    m_tabRenderer.SetRenderDpi(static_cast<float>(m_dpi));

    m_panelHwnd = CreateWindowExW(exStyle, kPanelClassName, L"EdgeDeck Panel", WS_POPUP,
                                   m_panelClosedXPx, m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                                   nullptr, nullptr, hInstance, nullptr);
    if (!m_panelHwnd) return;

    SetLayeredWindowAttributes(m_panelHwnd, 0, m_config.windowAlpha, LWA_ALPHA);
    ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                        m_config.cornerRadius * m_dpiScale);
    m_panelRenderer.AttachToWindow(m_panelHwnd);
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));

    // Panel starts hidden; only the tab is shown at launch.
    ShowWindow(m_tabHwnd, SW_SHOWNOACTIVATE);

    RegisterHotKey(m_tabHwnd, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_ALT, 'Q');
}

// ---------------------------------------------------------------------------
// Message loop / static dispatch
// ---------------------------------------------------------------------------

int EdgeWindow::RunMessageLoop() {
    MSG msg;
    BOOL result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (result == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK EdgeWindow::TabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (s_instance) return s_instance->HandleTabMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK EdgeWindow::PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (s_instance) return s_instance->HandlePanelMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Hover state machine
// ---------------------------------------------------------------------------

void EdgeWindow::OnEnter() {
    KillTimer(m_tabHwnd, kTimerLeave);

    if (m_state == State::Hidden) {
        BeginOpen();
    } else if (m_state == State::Closing) {
        // Cursor came back before the close animation finished - reverse it.
        m_animFromX = CurrentPanelX();
        m_animToX = m_panelOpenXPx;
        m_animStartTick = GetTickCount64();
        m_state = State::Opening;
        SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);
    }
}

void EdgeWindow::OnLeave() {
    // Debounce: don't close immediately, the cursor may just be crossing the
    // (borderless) seam between the tab window and the panel window.
    SetTimer(m_tabHwnd, kTimerLeave, static_cast<UINT>(m_config.closeDelayMs), nullptr);
}

void EdgeWindow::CheckPendingClose() {
    POINT pt;
    GetCursorPos(&pt);

    RECT tabRect{};
    GetWindowRect(m_tabHwnd, &tabRect);
    bool overTab = PtInRect(&tabRect, pt);

    bool overPanel = false;
    if (m_panelHwnd && IsWindowVisible(m_panelHwnd)) {
        RECT panelRect{};
        GetWindowRect(m_panelHwnd, &panelRect);
        overPanel = PtInRect(&panelRect, pt);
    }

    if (overTab || overPanel) return;

    if (m_state == State::Open || m_state == State::Opening) {
        BeginClose();
    }
}

void EdgeWindow::BeginOpen() {
    if (!m_panelHwnd) return;

    m_hoveredRow = -1;
    SetWindowPos(m_panelHwnd, HWND_TOPMOST, m_panelClosedXPx, m_panelYPx, m_panelWidthPx,
                 m_panelHeightPx, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    m_animFromX = m_panelClosedXPx;
    m_animToX = m_panelOpenXPx;
    m_animStartTick = GetTickCount64();
    m_state = State::Opening;
    SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);

    InvalidateRect(m_tabHwnd, nullptr, FALSE);
}

void EdgeWindow::BeginClose() {
    m_animFromX = CurrentPanelX();
    m_animToX = m_panelClosedXPx;
    m_animStartTick = GetTickCount64();
    m_state = State::Closing;
    SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);

    InvalidateRect(m_tabHwnd, nullptr, FALSE);
}

void EdgeWindow::StepAnimation() {
    ULONGLONG now = GetTickCount64();
    double t = static_cast<double>(now - m_animStartTick) / m_config.animationMs;
    t = std::clamp(t, 0.0, 1.0);
    double eased = 1.0 - std::pow(1.0 - t, 3.0); // ease-out cubic

    int x = m_animFromX + static_cast<int>(std::lround((m_animToX - m_animFromX) * eased));
    SetWindowPos(m_panelHwnd, nullptr, x, m_panelYPx, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (t >= 1.0) {
        KillTimer(m_tabHwnd, kTimerAnim);
        if (m_state == State::Opening) {
            m_state = State::Open;
        } else if (m_state == State::Closing) {
            ShowWindow(m_panelHwnd, SW_HIDE);
            m_state = State::Hidden;
        }
    }
}

int EdgeWindow::CurrentPanelX() const {
    RECT r{};
    GetWindowRect(m_panelHwnd, &r);
    return r.left;
}

// ---------------------------------------------------------------------------
// Panel content hit-testing / actions
// ---------------------------------------------------------------------------

int EdgeWindow::RowAt(int clientYPx) const {
    float logicalY = static_cast<float>(clientYPx) / m_dpiScale;
    if (logicalY < PanelLayout::TitleHeight) return -1;

    int idx = static_cast<int>((logicalY - PanelLayout::TitleHeight) / PanelLayout::RowHeight);
    if (idx < 0 || idx >= static_cast<int>(m_items.size())) return -1;
    return idx;
}

void EdgeWindow::InvokeRow(int index) {
    switch (index) {
        case 0: Actions::OpenNotepad(); break;
        case 1: Actions::OpenCalculator(); break;
        case 2: Actions::ShowDesktopToggle(); break;
        default: break;
    }
}

void EdgeWindow::ShowContextMenu(POINT screenPt) {
    constexpr UINT kMenuIdExit = 100; // distinct from kHotkeyId's WM_HOTKEY id space
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuIdExit, L"Exit");

    // Standard trick for a NOACTIVATE window's popup menu: briefly take the
    // foreground so the menu dismisses correctly on an outside click, then
    // post WM_NULL per MSDN so TrackPopupMenu's internal state settles.
    SetForegroundWindow(m_tabHwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, m_tabHwnd, nullptr);
    PostMessageW(m_tabHwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

// ---------------------------------------------------------------------------
// Tab window messages
// ---------------------------------------------------------------------------

LRESULT EdgeWindow::HandleTabMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (!m_tabTracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                m_tabTracking = true;
                m_tabHovered = true;
                KillTimer(m_tabHwnd, kTimerLeave);
                InvalidateRect(hwnd, nullptr, FALSE);
                OnEnter();
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            m_tabTracking = false;
            m_tabHovered = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            OnLeave();
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            ShowContextMenu(pt);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 100) { // "Exit" from the context menu
                DestroyWindow(m_tabHwnd);
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // avoid a redundant GDI fill before our D2D paint
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            bool expanded = (m_state == State::Open || m_state == State::Opening);
            const wchar_t* glyph = expanded ? L"‹" : L"›"; // ‹ / ›
            m_tabRenderer.DrawTab(m_tabHovered, m_config.tabWidth, m_config.tabHeight,
                                   m_config.cornerRadius, glyph);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == kTimerAnim) {
                StepAnimation();
                return 0;
            }
            if (wParam == kTimerLeave) {
                KillTimer(hwnd, kTimerLeave);
                CheckPendingClose();
                return 0;
            }
            break;
        }
        case WM_HOTKEY: {
            if (wParam == kHotkeyId) {
                DestroyWindow(m_tabHwnd);
                return 0;
            }
            break;
        }
        case WM_DESTROY: {
            UnregisterHotKey(hwnd, kHotkeyId);
            KillTimer(hwnd, kTimerAnim);
            KillTimer(hwnd, kTimerLeave);
            if (m_panelHwnd) {
                DestroyWindow(m_panelHwnd);
                m_panelHwnd = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Panel window messages
// ---------------------------------------------------------------------------

LRESULT EdgeWindow::HandlePanelMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (!m_panelTracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                m_panelTracking = true;
                KillTimer(m_tabHwnd, kTimerLeave);
                OnEnter();
            }
            int row = RowAt(GET_Y_LPARAM(lParam));
            if (row != m_hoveredRow) {
                m_hoveredRow = row;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            m_panelTracking = false;
            if (m_hoveredRow != -1) {
                m_hoveredRow = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            OnLeave();
            return 0;
        }
        case WM_LBUTTONUP: {
            int row = RowAt(GET_Y_LPARAM(lParam));
            if (row >= 0) InvokeRow(row);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            m_panelRenderer.DrawPanel(m_config.panelWidth, m_config.panelHeight,
                                       m_config.cornerRadius, m_items, m_hoveredRow);
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

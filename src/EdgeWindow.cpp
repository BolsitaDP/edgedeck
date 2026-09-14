#include "EdgeWindow.h"
#include "Actions.h"
#include "MediaControls.h"

#include <shellscalingapi.h>
#include <windowsx.h>
#include <cmath>
#include <algorithm>

namespace {
const wchar_t kTabClassName[] = L"EdgeDeckTabWindow";
const wchar_t kPanelClassName[] = L"EdgeDeckPanelWindow";

bool ApplyRoundedRegion(HWND hwnd, int widthPx, int heightPx, float radiusPx) {
    // CreateRoundRectRgn's last two params are the rounding ellipse's
    // width/height (diameter), not a radius - double it to match radiusPx.
    int diameter = static_cast<int>(std::lround(radiusPx * 2.0f));
    HRGN region = CreateRoundRectRgn(0, 0, widthPx + 1, heightPx + 1, diameter, diameter);
    if (!region) return false;
    if (!SetWindowRgn(hwnd, region, TRUE)) {
        DeleteObject(region);
        return false;
    }
    return true; // ownership transferred to the window
}
} // namespace

EdgeWindow* EdgeWindow::s_instance = nullptr;

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

EdgeWindow::~EdgeWindow() {
    if (m_tabHwnd && IsWindow(m_tabHwnd)) {
        DestroyWindow(m_tabHwnd);
    } else if (m_panelHwnd && IsWindow(m_panelHwnd)) {
        DestroyWindow(m_panelHwnd);
    }
    s_instance = nullptr;
}

bool EdgeWindow::Create(HINSTANCE hInstance) {
    s_instance = this;

    m_items = {
        {L"Open Notepad"},
        {L"Open Calculator"},
        {L"Show Desktop"},
    };
    m_config.panelHeight = PanelLayout::TitleHeight +
                            static_cast<float>(m_items.size()) * PanelLayout::RowHeight +
                            PanelLayout::SpotifyHeight +
                            PanelLayout::BottomPadding;

    return RegisterClasses(hInstance) && ComputeLayout() && CreateWindows(hInstance);
}

bool EdgeWindow::RegisterClasses(HINSTANCE hInstance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint the whole client area ourselves

    wc.lpszClassName = kTabClassName;
    wc.lpfnWndProc = TabProc;
    if (!RegisterClassExW(&wc)) return false;

    wc.lpszClassName = kPanelClassName;
    wc.lpfnWndProc = PanelProc;
    return RegisterClassExW(&wc) != 0;
}

bool EdgeWindow::ComputeLayout() {
    POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfo(monitor, &mi)) return false;

    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        dpiX = 96;
    }
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
    m_panelYPx = std::clamp(m_panelYPx, monTop, std::max(monTop, monBottom - m_panelHeightPx));

    m_panelOpenXPx = tabX - m_panelWidthPx; // resting position, left of the tab
    m_panelClosedXPx = monRight;            // fully outside the edge until it slides in
    return true;
}

bool EdgeWindow::CreateWindows(HINSTANCE hInstance) {
    const DWORD tabExStyle =
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    const DWORD panelExStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;

    m_tabHwnd = CreateWindowExW(
        tabExStyle, kTabClassName, L"EdgeDeck", WS_POPUP, m_tabRectPx.left, m_tabRectPx.top,
        m_tabRectPx.right - m_tabRectPx.left, m_tabRectPx.bottom - m_tabRectPx.top, nullptr,
        nullptr, hInstance, nullptr);
    if (!m_tabHwnd) return false;

    if (!SetLayeredWindowAttributes(m_tabHwnd, 0, m_config.windowAlpha, LWA_ALPHA) ||
        !ApplyRoundedRegion(m_tabHwnd, m_tabRectPx.right - m_tabRectPx.left,
                            m_tabRectPx.bottom - m_tabRectPx.top,
                            m_config.cornerRadius * m_dpiScale) ||
        !m_tabRenderer.AttachToWindow(m_tabHwnd)) return false;
    m_tabRenderer.SetRenderDpi(static_cast<float>(m_dpi));

    m_panelHwnd = CreateWindowExW(panelExStyle, kPanelClassName, L"EdgeDeck Panel", WS_POPUP,
                                   m_panelClosedXPx, m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                                   nullptr, nullptr, hInstance, nullptr);
    if (!m_panelHwnd) return false;

    if (!SetLayeredWindowAttributes(m_panelHwnd, 0, m_config.windowAlpha, LWA_ALPHA) ||
        !ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                            m_config.cornerRadius * m_dpiScale) ||
        !m_panelRenderer.AttachToWindow(m_panelHwnd)) return false;
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));

    // Panel starts hidden; only the tab is shown at launch.
    ShowWindow(m_tabHwnd, SW_SHOWNOACTIVATE);

    bool exitHotkey = RegisterHotKey(m_tabHwnd, kHotkeyExitId,
                                     MOD_CONTROL | MOD_SHIFT | MOD_ALT, 'Q') != 0;
    bool toggleHotkey = RegisterHotKey(m_tabHwnd, kHotkeyToggleId,
                                       MOD_CONTROL | MOD_SHIFT | MOD_ALT, 'E') != 0;
    if (!exitHotkey || !toggleHotkey) {
        MessageBoxW(m_tabHwnd, L"One or more EdgeDeck keyboard shortcuts are unavailable.",
                    L"EdgeDeck", MB_ICONWARNING | MB_OK | MB_TOPMOST);
    }
    return true;
}

void EdgeWindow::Relayout() {
    if (m_inRelayout || !m_tabHwnd || !m_panelHwnd) return;
    m_inRelayout = true;
    if (!ComputeLayout()) {
        m_inRelayout = false;
        return;
    }

    // Display changes are uncommon; finish an in-flight slide at its endpoint.
    KillTimer(m_tabHwnd, kTimerAnim);
    const bool panelOpen = m_state == State::Open || m_state == State::Opening;
    m_state = panelOpen ? State::Open : State::Hidden;
    m_hoveredRow = -1;
    m_selectedSpotifyButton = -1;
    TRACKMOUSEEVENT tabLeave{sizeof(tabLeave), TME_CANCEL | TME_LEAVE, m_tabHwnd, 0};
    TRACKMOUSEEVENT panelLeave{sizeof(panelLeave), TME_CANCEL | TME_LEAVE, m_panelHwnd, 0};
    TrackMouseEvent(&tabLeave);
    TrackMouseEvent(&panelLeave);
    m_tabTracking = false;
    m_panelTracking = false;
    m_tabHovered = false;

    const int tabWidth = m_tabRectPx.right - m_tabRectPx.left;
    const int tabHeight = m_tabRectPx.bottom - m_tabRectPx.top;
    SetWindowPos(m_tabHwnd, HWND_TOPMOST, m_tabRectPx.left, m_tabRectPx.top,
                 tabWidth, tabHeight, SWP_NOACTIVATE);
    SetWindowPos(m_panelHwnd, m_tabHwnd,
                 panelOpen ? m_panelOpenXPx : m_panelClosedXPx,
                 m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                 SWP_NOACTIVATE | (panelOpen ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

    ApplyRoundedRegion(m_tabHwnd, tabWidth, tabHeight, m_config.cornerRadius * m_dpiScale);
    ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                       m_config.cornerRadius * m_dpiScale);
    m_tabRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    m_tabRenderer.OnResize(static_cast<UINT>(tabWidth), static_cast<UINT>(tabHeight));
    m_panelRenderer.OnResize(static_cast<UINT>(m_panelWidthPx),
                             static_cast<UINT>(m_panelHeightPx));
    InvalidateRect(m_tabHwnd, nullptr, FALSE);
    InvalidateRect(m_panelHwnd, nullptr, FALSE);
    m_inRelayout = false;
}

// ---------------------------------------------------------------------------
// Message loop / static dispatch
// ---------------------------------------------------------------------------

int EdgeWindow::RunMessageLoop() {
    MSG msg{};
    BOOL result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (result == -1) return 1;
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
        InvalidateRect(m_tabHwnd, nullptr, FALSE);
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

    if (overTab || overPanel || m_panelPinned) return;

    if (m_state == State::Open || m_state == State::Opening) {
        BeginClose();
    }
}

void EdgeWindow::BeginOpen() {
    if (!m_panelHwnd) return;

    m_hoveredRow = -1;
    m_selectedSpotifyButton = -1;
    SetWindowPos(m_panelHwnd, m_tabHwnd, m_panelClosedXPx, m_panelYPx, m_panelWidthPx,
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

void EdgeWindow::TogglePanel() {
    KillTimer(m_tabHwnd, kTimerLeave);
    if (m_panelPinned) {
        m_panelPinned = false;
        BeginClose();
        return;
    }

    m_panelPinned = true;
    OnEnter();
    m_hoveredRow = 0;
    m_selectedSpotifyButton = -1;
    InvalidateRect(m_panelHwnd, nullptr, FALSE);
    SetForegroundWindow(m_panelHwnd);
    SetFocus(m_panelHwnd);
}

void EdgeWindow::StepAnimation() {
    ULONGLONG now = GetTickCount64();
    double t = static_cast<double>(now - m_animStartTick) / m_config.animationMs;
    t = std::clamp(t, 0.0, 1.0);
    double remaining = 1.0 - t;
    double eased = 1.0 - remaining * remaining * remaining; // ease-out cubic

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

int EdgeWindow::SpotifyButtonAt(int clientXPx, int clientYPx) const {
    float x = static_cast<float>(clientXPx) / m_dpiScale;
    float y = static_cast<float>(clientYPx) / m_dpiScale;
    float top = PanelLayout::TitleHeight +
                static_cast<float>(m_items.size()) * PanelLayout::RowHeight +
                PanelLayout::SpotifyHeaderHeight;
    if (y < top || y >= top + PanelLayout::SpotifyButtonHeight) return -1;

    float buttonWidth = (m_config.panelWidth - 2.0f * PanelLayout::PaddingX -
                         2.0f * PanelLayout::SpotifyButtonGap) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        float left = PanelLayout::PaddingX +
                     static_cast<float>(i) * (buttonWidth + PanelLayout::SpotifyButtonGap);
        if (x >= left && x < left + buttonWidth) return i;
    }
    return -1;
}

void EdgeWindow::InvokeRow(int index) {
    bool succeeded = true;
    switch (index) {
        case 0: succeeded = Actions::OpenNotepad(); break;
        case 1: succeeded = Actions::OpenCalculator(); break;
        case 2: succeeded = Actions::ShowDesktopToggle(); break;
        default: break;
    }
    if (!succeeded) {
        MessageBoxW(m_panelHwnd, L"Windows could not complete this action.", L"EdgeDeck",
                    MB_ICONERROR | MB_OK | MB_TOPMOST);
    }
}

void EdgeWindow::InvokeSpotifyButton(int index) {
    if (m_spotifyBusy) return;
    MediaControls::SpotifyCommand command;
    switch (index) {
        case 0: command = MediaControls::SpotifyCommand::Previous; break;
        case 1: command = MediaControls::SpotifyCommand::PlayPause; break;
        case 2: command = MediaControls::SpotifyCommand::Next; break;
        default: return;
    }
    m_spotifyBusy = true;
    MediaControls::SendSpotifyCommand(command, m_tabHwnd, kSpotifyResultMessage);
}

void EdgeWindow::ShowContextMenu(POINT screenPt) {
    constexpr UINT kMenuIdExit = 100; // distinct from the WM_HOTKEY ids
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
        case WM_LBUTTONUP:
            TogglePanel();
            return 0;
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
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            Relayout();
            return 0;
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
            if (wParam == kHotkeyExitId) {
                DestroyWindow(m_tabHwnd);
                return 0;
            }
            if (wParam == kHotkeyToggleId) {
                TogglePanel();
                return 0;
            }
            break;
        }
        case kSpotifyResultMessage: {
            m_spotifyBusy = false;
            auto result = static_cast<MediaControls::Result>(wParam);
            if (result == MediaControls::Result::Success) return 0;

            const wchar_t* message = L"Windows could not control Spotify.";
            if (result == MediaControls::Result::SpotifyNotFound) {
                message = L"Open Spotify and start a song to enable these controls.";
            } else if (result == MediaControls::Result::Unsupported) {
                message = L"Spotify does not support this control right now.";
            }
            MessageBoxW(m_tabHwnd, message, L"EdgeDeck", MB_ICONWARNING | MB_OK | MB_TOPMOST);
            return 0;
        }
        case WM_DESTROY: {
            UnregisterHotKey(hwnd, kHotkeyExitId);
            UnregisterHotKey(hwnd, kHotkeyToggleId);
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
            int spotifyButton = SpotifyButtonAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (row != m_hoveredRow || spotifyButton != m_selectedSpotifyButton) {
                m_hoveredRow = row;
                m_selectedSpotifyButton = spotifyButton;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            m_panelTracking = false;
            if (GetForegroundWindow() != m_panelHwnd &&
                (m_hoveredRow != -1 || m_selectedSpotifyButton != -1)) {
                m_hoveredRow = -1;
                m_selectedSpotifyButton = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            OnLeave();
            return 0;
        }
        case WM_LBUTTONUP: {
            int row = RowAt(GET_Y_LPARAM(lParam));
            int spotifyButton = SpotifyButtonAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (row >= 0) {
                InvokeRow(row);
                m_panelPinned = false;
                if (m_state == State::Open || m_state == State::Opening) BeginClose();
            } else if (spotifyButton >= 0) {
                InvokeSpotifyButton(spotifyButton);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const int count = static_cast<int>(m_items.size());
            const int total = count + 3;
            if (wParam == VK_ESCAPE) {
                m_panelPinned = false;
                BeginClose();
                return 0;
            }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                int selected = m_selectedSpotifyButton >= 0 ?
                               count + m_selectedSpotifyButton : m_hoveredRow;
                if (wParam == VK_DOWN) {
                    selected = (selected + 1) % total;
                } else {
                    selected = selected < 0 ? total - 1 : (selected + total - 1) % total;
                }
                m_hoveredRow = selected < count ? selected : -1;
                m_selectedSpotifyButton = selected >= count ? selected - count : -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                if (m_selectedSpotifyButton >= 0) {
                    InvokeSpotifyButton(m_selectedSpotifyButton);
                } else if (count > 0) {
                    InvokeRow(m_hoveredRow < 0 ? 0 : m_hoveredRow);
                    m_panelPinned = false;
                    if (m_state == State::Open || m_state == State::Opening) BeginClose();
                }
                return 0;
            }
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && m_panelPinned) {
                m_panelPinned = false;
                if (m_state == State::Open || m_state == State::Opening) BeginClose();
            }
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            Relayout();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            m_panelRenderer.DrawPanel(m_config.panelWidth, m_config.panelHeight,
                                       m_config.cornerRadius, m_items, m_hoveredRow,
                                       m_selectedSpotifyButton);
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

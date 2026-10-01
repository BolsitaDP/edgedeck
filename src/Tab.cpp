#include "Diagnostics.h"
#include "Tab.h"
#include "App.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace {
// Layered (so the panel can be translucent), topmost, tool-window and - the
// important one - never activate: hover, click or pin on a tab must not pull
// focus away from whatever the user is actually doing.
const DWORD kWindowExStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;

// Whole-window alpha. The old value of 235 washed out the text along with the
// background; 246 keeps a hint of translucency while staying legible over any
// wallpaper.
constexpr BYTE kWindowAlpha = 246;

// Rounded corners come from a window region, which is a 1-bit mask: the edges
// are visibly jagged at 125% scaling and above. Doing it properly needs
// per-pixel alpha, which needs a DXGI-backed D2D device (see the note on the
// Renderer class), so the region stays for now.
bool ApplyRoundedRegion(HWND hwnd, int widthPx, int heightPx, float radiusPx) {
    // CreateRoundRectRgn's last two params are the rounding ellipse's
    // width/height (its diameter), not a radius - double it to match radiusPx.
    const int diameter = static_cast<int>(std::lround(radiusPx * 2.0f));

    // Its right and bottom are *exclusive*, so (0, 0, widthPx, heightPx) already
    // covers exactly widthPx x heightPx pixels. Adding 1 here made every window 1px
    // larger than the layout asked for, and SetWindowRgn resizes the window to
    // match the region, so GetClientRect then disagreed with Tab's own geometry
    // for the life of the window - which is what the defensive comment in
    // DrawPanel about the window being the authority on its own size was working
    // around.
    HRGN region = CreateRoundRectRgn(0, 0, widthPx, heightPx, diameter, diameter);
    if (!region) return false;
    if (!SetWindowRgn(hwnd, region, TRUE)) {
        DeleteObject(region);
        return false;
    }
    return true; // ownership transferred to the window
}
} // namespace

Tab::Tab(App* owner, TabConfig config, std::unique_ptr<PanelWidget> widget)
    : m_config(config), m_widget(std::move(widget)), m_owner(owner) {}

Tab::~Tab() {
    if (m_tabHwnd && IsWindow(m_tabHwnd)) {
        DestroyWindow(m_tabHwnd);
    } else if (m_panelHwnd && IsWindow(m_panelHwnd)) {
        DestroyWindow(m_panelHwnd);
    }
}

bool Tab::RegisterClasses(HINSTANCE hInstance) {
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

void Tab::UnregisterClasses() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    UnregisterClassW(kTabClassName, instance);
    UnregisterClassW(kPanelClassName, instance);
}

bool Tab::Create(HINSTANCE hInstance) { return ComputeLayout() && CreateWindows(hInstance); }

void Tab::SetMonitor(HMONITOR monitor) {
    if (m_monitor == monitor) return;
    m_monitor = monitor;
    Relayout();
}

bool Tab::ComputeLayout() {
    HMONITOR monitor =
        m_monitor ? m_monitor : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    m_monitor = monitor;

    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfo(monitor, &mi)) return false;

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    m_dpi = dpiX;
    m_dpiScale = static_cast<float>(dpiX) / 96.0f;

    const int monRight = mi.rcMonitor.right;
    const int monTop = mi.rcMonitor.top;
    const int monBottom = mi.rcMonitor.bottom;
    const int monHeight = monBottom - monTop;

    const int tabWpx = static_cast<int>(std::lround(m_config.tabWidth * m_dpiScale));
    const int tabHpx = static_cast<int>(std::lround(m_config.tabHeight * m_dpiScale));

    // A widget that cannot lay out legibly in a narrow panel says so, so the
    // configured width is raised to fit rather than silently producing
    // unreadable rows - the old floor of 200 logical pixels left the media
    // widget with a 38-pixel-wide name field.
    if (m_widget) {
        const float chrome = PanelLayout::PaddingX * 2.0f;
        m_config.panelWidth = std::max(m_config.panelWidth, m_widget->MinContentWidth() + chrome);
    }

    m_panelHeightLogical =
        PanelLayout::ChromeHeight +
        (m_widget ? m_widget->PreferredContentHeight(m_config.panelWidth) : 0.0f) +
        PanelLayout::BottomPadding;

    m_panelWidthPx = static_cast<int>(std::lround(m_config.panelWidth * m_dpiScale));
    m_panelHeightPx = static_cast<int>(std::lround(m_panelHeightLogical * m_dpiScale));

    // Right-edge placement (the only edge this app supports for now).
    const int tabX = monRight - tabWpx;
    const int tabY = monTop + static_cast<int>((monHeight - tabHpx) * m_config.verticalRatio);
    m_tabRectPx = {tabX, tabY, tabX + tabWpx, tabY + tabHpx};

    ClampPanelY();

    m_panelOpenXPx = tabX - m_panelWidthPx; // resting position, left of the tab
    m_panelClosedXPx = monRight;            // fully outside the edge until it slides in
    return true;
}

void Tab::ClampPanelY() {
    MONITORINFO mi{sizeof(mi)};
    if (!m_monitor || !GetMonitorInfo(m_monitor, &mi)) return;
    const int centerY = (m_tabRectPx.top + m_tabRectPx.bottom) / 2;
    const int wanted = centerY - m_panelHeightPx / 2;
    const int lowest = static_cast<int>(mi.rcMonitor.bottom) - m_panelHeightPx;
    m_panelYPx = std::clamp<int>(wanted, static_cast<int>(mi.rcMonitor.top), lowest);
}

bool Tab::CreateWindows(HINSTANCE hInstance) {
    const int tabWidth = m_tabRectPx.right - m_tabRectPx.left;
    const int tabHeight = m_tabRectPx.bottom - m_tabRectPx.top;

    m_tabHwnd = CreateWindowExW(kWindowExStyle, kTabClassName, L"EdgeDeck", WS_POPUP,
                                m_tabRectPx.left, m_tabRectPx.top, tabWidth, tabHeight, nullptr,
                                nullptr, hInstance, this);
    if (!m_tabHwnd) return false;
    if (!m_tabRenderer.AttachToWindow(m_tabHwnd, true)) {
        Diagnostics::Error("Tab: could not create text formats for the edge tab");
    }
    m_tabRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    SetLayeredWindowAttributes(m_tabHwnd, 0, kWindowAlpha, LWA_ALPHA);
    ApplyRoundedRegion(m_tabHwnd, tabWidth, tabHeight, m_config.cornerRadius * m_dpiScale);

    m_panelHwnd = CreateWindowExW(kWindowExStyle, kPanelClassName, L"EdgeDeck Panel", WS_POPUP,
                                   m_panelClosedXPx, m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                                   nullptr, nullptr, hInstance, this);
    if (!m_panelHwnd) return false;
    if (!m_panelRenderer.AttachToWindow(m_panelHwnd, true)) {
        Diagnostics::Error("Tab: could not create text formats for the panel");
    }
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    SetLayeredWindowAttributes(m_panelHwnd, 0, kWindowAlpha, LWA_ALPHA);
    ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx, m_config.cornerRadius * m_dpiScale);

    ShowWindow(m_tabHwnd, SW_SHOWNOACTIVATE);

    // Park the panel just off the edge; it stays hidden until the first hover.
    SetWindowPos(m_panelHwnd, m_tabHwnd, m_panelClosedXPx, m_panelYPx, m_panelWidthPx,
                 m_panelHeightPx, SWP_NOACTIVATE | SWP_HIDEWINDOW);
    return true;
}

void Tab::Relayout() {
    if (m_inRelayout || !m_tabHwnd || !m_panelHwnd) return;
    m_inRelayout = true;

    KillTimer(m_tabHwnd, kTimerAnim);

    const bool panelOpen = (m_state == State::Open || m_state == State::Opening);
    m_state = panelOpen ? State::Open : State::Hidden;
    m_hoveredControl = -1;
    m_focusedControl = -1;
    if (m_widget) m_widget->SetHovered(-1);

    TRACKMOUSEEVENT tabLeave{sizeof(tabLeave), TME_CANCEL | TME_LEAVE, m_tabHwnd, 0};
    TRACKMOUSEEVENT panelLeave{sizeof(panelLeave), TME_CANCEL | TME_LEAVE, m_panelHwnd, 0};
    TrackMouseEvent(&tabLeave);
    TrackMouseEvent(&panelLeave);
    m_tabTracking = false;
    m_panelTracking = false;
    m_tabHovered = false;

    if (!ComputeLayout()) {
        m_inRelayout = false;
        return;
    }

    const int tabWidth = m_tabRectPx.right - m_tabRectPx.left;
    const int tabHeight = m_tabRectPx.bottom - m_tabRectPx.top;
    SetWindowPos(m_tabHwnd, HWND_TOPMOST, m_tabRectPx.left, m_tabRectPx.top, tabWidth, tabHeight,
                 SWP_NOACTIVATE);
    SetWindowPos(m_panelHwnd, m_tabHwnd, panelOpen ? m_panelOpenXPx : m_panelClosedXPx, m_panelYPx,
                 m_panelWidthPx, m_panelHeightPx,
                 SWP_NOACTIVATE | (panelOpen ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

    m_tabRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    m_tabRenderer.OnResize(static_cast<UINT>(tabWidth), static_cast<UINT>(tabHeight));
    m_panelRenderer.OnResize(static_cast<UINT>(m_panelWidthPx), static_cast<UINT>(m_panelHeightPx));
    ApplyRoundedRegion(m_tabHwnd, tabWidth, tabHeight, m_config.cornerRadius * m_dpiScale);
    ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                       m_config.cornerRadius * m_dpiScale);

    m_tabRenderer.DrawTab(m_tabHovered, m_config.tabWidth, m_config.tabHeight,
                          m_widget ? m_widget->TabGlyph() : L"?");
    if (panelOpen) DrawPanelSurface();
    UpdateTickTimer();
    m_inRelayout = false;
}

void Tab::ApplyConfig(const TabConfig& config) {
    m_config = config;
    if (!m_tabHwnd || !m_panelHwnd) return;
    Relayout();
    if (m_state == State::Open) DrawPanelSurface();
}

void Tab::ResizePanelToContent() {
    if (!m_widget || !m_panelHwnd) return;

    const float newHeightLogical = PanelLayout::ChromeHeight +
                                   m_widget->PreferredContentHeight(m_config.panelWidth) +
                                   PanelLayout::BottomPadding;
    const int newHeightPx = static_cast<int>(std::lround(newHeightLogical * m_dpiScale));
    if (newHeightPx == m_panelHeightPx) return;

    m_panelHeightLogical = newHeightLogical;
    m_panelHeightPx = newHeightPx;
    ClampPanelY();

    // X is left alone: a slide may still be in flight and must not be snapped to
    // its resting position.
    SetWindowPos(m_panelHwnd, nullptr, 0, 0, m_panelWidthPx, m_panelHeightPx,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                       m_config.cornerRadius * m_dpiScale);
    m_panelRenderer.OnResize(static_cast<UINT>(m_panelWidthPx), static_cast<UINT>(m_panelHeightPx));

    if (m_state == State::Open || m_state == State::Opening) DrawPanelSurface();
}

// ---------------------------------------------------------------------------
// Static dispatch (per-window GWLP_USERDATA - Tab is not a singleton)
// ---------------------------------------------------------------------------

LRESULT CALLBACK Tab::TabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Tab* self = reinterpret_cast<Tab*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Tab*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleTabMessage(hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK Tab::PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Tab* self = reinterpret_cast<Tab*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Tab*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandlePanelMessage(hwnd, msg, wParam, lParam)
                : DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Hover / pin state machine
// ---------------------------------------------------------------------------

void Tab::OnEnter() {
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

void Tab::OnLeave() {
    // Debounce: don't close immediately, the cursor may just be crossing the
    // (borderless) seam between the tab window and the panel window.
    SetTimer(m_tabHwnd, kTimerLeave, static_cast<UINT>(m_config.closeDelayMs), nullptr);
}

void Tab::CheckPendingClose() {
    POINT pt;
    GetCursorPos(&pt);
    if (IsPointInside(pt) || m_pinned || m_dragging) return;
    if (m_state == State::Open || m_state == State::Opening) BeginClose();
}

bool Tab::IsPointInside(POINT screenPt) const {
    if (!m_tabHwnd) return false;
    RECT tabRect{};
    GetWindowRect(m_tabHwnd, &tabRect);
    if (PtInRect(&tabRect, screenPt)) return true;

    if (m_panelHwnd && IsWindowVisible(m_panelHwnd)) {
        RECT panelRect{};
        GetWindowRect(m_panelHwnd, &panelRect);
        if (PtInRect(&panelRect, screenPt)) return true;
    }
    return false;
}

void Tab::BeginOpen() {
    if (!m_panelHwnd) return;

    m_hoveredControl = -1;
    if (m_widget) {
        m_widget->SetHovered(-1);
        m_widget->OnPanelOpening(m_tabHwnd);
    }

    m_animFromX = m_panelClosedXPx;
    m_animToX = m_panelOpenXPx;
    m_animStartTick = GetTickCount64();
    m_state = State::Opening;
    SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);

    // The panel has to be shown before it can be painted: InvalidateRect on a
    // hidden window never produces a WM_PAINT, so without this the slide would
    // animate an empty rectangle. SW_SHOWNOACTIVATE keeps the WS_EX_NOACTIVATE
    // promise - opening a panel must not take focus from the foreground app.
    ShowWindow(m_panelHwnd, SW_SHOWNOACTIVATE);
    InvalidateRect(m_panelHwnd, nullptr, FALSE);
}

void Tab::BeginClose() {
    if (m_state == State::Hidden || m_state == State::Closing) return;

    m_animFromX = CurrentPanelX();
    m_animToX = m_panelClosedXPx;
    m_animStartTick = GetTickCount64();
    m_state = State::Closing;
    SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);
    if (m_widget) m_widget->OnPanelVisibilityChanged(false);
    UpdateTickTimer();
}

void Tab::TogglePin() {
    m_pinned = !m_pinned;
    if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);

    if (!m_pinned) {
        // Unpinned via the button itself; if the cursor already left both
        // windows, close now instead of waiting for a leave event that already
        // happened while the panel was pinned open.
        POINT pt;
        GetCursorPos(&pt);
        if (!IsPointInside(pt) && (m_state == State::Open || m_state == State::Opening)) {
            BeginClose();
        }
    }
}

void Tab::RequestOpen() {
    if (!m_panelHwnd) return;
    OnEnter();
    // A keyboard-opened panel has no cursor over it, so nothing would ever start
    // the leave countdown. Pin it instead, and let Escape close it.
    if (m_state != State::Hidden) {
        m_pinned = true;
        InvalidateRect(m_panelHwnd, nullptr, FALSE);
    }
}

void Tab::EndDrag() {
    if (!m_dragging) return;
    m_dragging = false; // first, so WM_CAPTURECHANGED from ReleaseCapture is a no-op
    ReleaseCapture();
    if (m_widget) m_widget->OnDragEnd();
    if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);

    // The cursor may have left the panel while dragging; the leave events were
    // swallowed by the capture, so start the normal close countdown.
    POINT pt;
    GetCursorPos(&pt);
    if (!IsPointInside(pt)) OnLeave();
}

void Tab::UpdateTickTimer() {
    if (!m_tabHwnd) return;
    const bool wantsTick = m_widget && m_widget->WantsTicks() &&
                           (m_state == State::Open || m_state == State::Opening);
    if (wantsTick) {
        SetTimer(m_tabHwnd, kTimerTick, kTickIntervalMs, nullptr);
    } else {
        KillTimer(m_tabHwnd, kTimerTick);
    }
}

void Tab::MovePanelTo(int x) {
    if (!m_panelHwnd || !IsWindowVisible(m_panelHwnd)) return;
    // Reposition only (SWP_NOSIZE): a slide costs one cheap call per frame
    // instead of a full relayout of a topmost layered window.
    SetWindowPos(m_panelHwnd, nullptr, x, m_panelYPx, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Tab::StepAnimation() {
    const ULONGLONG now = GetTickCount64();
    const double duration = m_config.animationMs > 0 ? m_config.animationMs : 1;
    double t = static_cast<double>(now - m_animStartTick) / duration;
    t = std::clamp(t, 0.0, 1.0);
    const double remaining = 1.0 - t;
    const double eased = 1.0 - remaining * remaining * remaining; // ease-out cubic

    const int x =
        m_animFromX + static_cast<int>(std::lround((m_animToX - m_animFromX) * eased));
    MovePanelTo(x);

    if (t < 1.0) return;

    KillTimer(m_tabHwnd, kTimerAnim);
    if (m_state == State::Opening) {
        m_state = State::Open;
        if (m_widget) m_widget->OnPanelVisibilityChanged(true);
    } else if (m_state == State::Closing) {
        ShowWindow(m_panelHwnd, SW_HIDE);
        m_state = State::Hidden;
        if (m_widget) m_widget->OnPanelVisibilityChanged(false);
    }
    UpdateTickTimer();
}

int Tab::CurrentPanelX() const {
    RECT r{};
    if (!m_panelHwnd || !GetWindowRect(m_panelHwnd, &r)) return m_panelOpenXPx;
    return r.left;
}

void Tab::DrawPanelSurface() {
    if (!m_panelHwnd || !m_widget) return;
    m_panelRenderer.DrawPanel(m_config.panelWidth, m_panelHeightLogical, m_widget.get(), m_pinned,
                              m_hoveredControl == kPinControlId);
    if (m_state == State::Open) MovePanelTo(m_panelOpenXPx);
}

// ---------------------------------------------------------------------------
// Panel hit-testing (chrome pin button, then delegate to the widget)
// ---------------------------------------------------------------------------

int Tab::HitTestPanel(int clientXPx, int clientYPx) const {
    const float x = static_cast<float>(clientXPx) / m_dpiScale;
    const float y = static_cast<float>(clientYPx) / m_dpiScale;

    const D2D1_RECT_F pinRect = PanelLayout::PinButtonRect(m_config.panelWidth);
    if (x >= pinRect.left && x <= pinRect.right && y >= pinRect.top && y <= pinRect.bottom) {
        return kPinControlId;
    }

    if (y < PanelLayout::ChromeHeight || !m_widget) return -1;

    const float contentHeight =
        m_panelHeightLogical - PanelLayout::ChromeHeight - PanelLayout::BottomPadding;
    return m_widget->HitTest(x, y - PanelLayout::ChromeHeight, m_config.panelWidth, contentHeight);
}

// ---------------------------------------------------------------------------
// Tab window messages
// ---------------------------------------------------------------------------

LRESULT Tab::HandleTabMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
            if (m_owner) m_owner->ShowTabContextMenu(this, pt);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // avoid a redundant GDI fill before our D2D paint
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            // Theme, contrast and DPI changes all land here, and all of them
            // change the palette or the metrics the layout was computed from.
            //
            // The palette refresh lives here rather than in the paint path: it is
            // a registry read plus a SystemParametersInfo, and doing it per paint
            // meant doing it about fifteen times per animation frame. Refresh()
            // reports whether anything actually changed, so a settings change that
            // does not affect the palette skips the relayout entirely.
            if (PanelTheme::Refresh()) Relayout();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            m_tabRenderer.DrawTab(m_tabHovered, m_config.tabWidth, m_config.tabHeight,
                                  m_widget ? m_widget->TabGlyph() : L"?");
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
            if (wParam == kTimerTick) {
                if (m_widget && (m_state == State::Open || m_state == State::Opening)) {
                    m_widget->OnTick();
                    if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        }
        case WM_DESTROY: {
            KillTimer(hwnd, kTimerAnim);
            KillTimer(hwnd, kTimerLeave);
            KillTimer(hwnd, kTimerTick);
            if (m_panelHwnd) {
                DestroyWindow(m_panelHwnd);
                m_panelHwnd = nullptr;
            }
            return 0;
        }
        default:
            // Only the widget's private range is forwarded. Anything else at or
            // above WM_APP belongs to somebody else and must not be swallowed.
            if (PanelWidget::IsWidgetMessage(msg) && m_widget) {
                m_widget->OnAsyncResult(msg, wParam);
                ResizePanelToContent();
                if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Panel window messages
// ---------------------------------------------------------------------------

LRESULT Tab::HandlePanelMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (!m_panelTracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                m_panelTracking = true;
                KillTimer(m_tabHwnd, kTimerLeave);
                OnEnter();
            }
            if (m_dragging && m_widget) {
                const float x = static_cast<float>(GET_X_LPARAM(lParam)) / m_dpiScale;
                const float y =
                    static_cast<float>(GET_Y_LPARAM(lParam)) / m_dpiScale - PanelLayout::ChromeHeight;
                const float contentHeight = m_panelHeightLogical - PanelLayout::ChromeHeight -
                                            PanelLayout::BottomPadding;
                m_widget->OnDragMove(x, y, m_config.panelWidth, contentHeight);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            {
                const int hit = HitTestPanel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hit != m_hoveredControl) {
                    m_hoveredControl = hit;
                    if (m_widget) m_widget->SetHovered(hit >= 0 ? hit : -1);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            m_panelTracking = false;
            if (m_hoveredControl != -1) {
                m_hoveredControl = -1;
                if (m_widget) m_widget->SetHovered(-1);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            OnLeave();
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!m_widget) return 0;
            const float x = static_cast<float>(GET_X_LPARAM(lParam)) / m_dpiScale;
            const float y =
                static_cast<float>(GET_Y_LPARAM(lParam)) / m_dpiScale - PanelLayout::ChromeHeight;
            if (y < 0.0f) return 0; // press on the title/pin chrome, not the content
            const float contentHeight = m_panelHeightLogical - PanelLayout::ChromeHeight -
                                        PanelLayout::BottomPadding;
            if (m_widget->OnDragBegin(x, y, m_config.panelWidth, contentHeight, m_tabHwnd)) {
                m_dragging = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (m_dragging && reinterpret_cast<HWND>(lParam) != hwnd) EndDrag();
            return 0;
        case WM_LBUTTONUP: {
            if (m_dragging) {
                EndDrag();
                return 0;
            }
            const int hit = HitTestPanel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (hit == kPinControlId) {
                TogglePin();
            } else if (hit >= 0 && m_widget) {
                // Acting never closes the panel: while the cursor is over the
                // tab it stays open, and it closes only once the cursor leaves
                // (or never, if pinned).
                m_widget->Activate(hit, m_tabHwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            // The panel never takes focus, so it is driven straight from the
            // keyboard while the pointer is over it. This is what makes every
            // widget reachable without a mouse.
            if (!m_widget) return 0;
            const UINT key = static_cast<UINT>(wParam);

            if (key == VK_ESCAPE) {
                m_pinned = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                BeginClose();
                return 0;
            }

            if (m_widget->FocusableControlCount() > 0 && m_focusedControl < 0) {
                m_focusedControl = 0;
            }

            if ((key == VK_LEFT || key == VK_RIGHT) && m_focusedControl >= 0) {
                float value01 = 0.0f;
                if (m_widget->OnStepControl(m_focusedControl, key == VK_RIGHT ? 1 : -1, value01)) {
                    m_hoveredControl = m_focusedControl;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (m_widget->OnKeyDown(key, m_focusedControl)) {
                const int focused = m_widget->FocusedControl();
                if (focused >= 0) {
                    m_focusedControl = focused;
                    m_hoveredControl = focused;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            // The wheel steps the focused slider, which is the one adjustment
            // that benefits most from not needing a drag.
            if (!m_widget || m_widget->FocusableControlCount() == 0) return 0;
            if (m_focusedControl < 0) m_focusedControl = 0;
            float value01 = 0.0f;
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;
            if (m_widget->OnStepControl(m_focusedControl, delta, value01)) {
                m_hoveredControl = m_focusedControl;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            // The tab window handles these too, and whichever one is reached
            // first does the single refresh; the other sees no change and skips
            // its relayout.
            if (PanelTheme::Refresh()) Relayout();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            DrawPanelSurface();
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

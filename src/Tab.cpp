#include "Config.h"
#include "Diagnostics.h"
#include "DisplayTopology.h"
#include "Fullscreen.h"
#include "Tab.h"
#include "App.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace {
// Layered (so the panel can be translucent), topmost, tool-window and - the
// important one - never activate: hover, click or pin on a tab must not pull
// focus away from whatever the user is actually doing.
const DWORD kWindowExStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;

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

// Rounded corners, by whichever means this window's renderer uses.
//
// On the per-pixel-alpha path the shape comes from the alpha in the rendered
// bitmap, so there is no region to set and no constant window alpha to apply -
// a region clips the alpha away again, and UpdateLayeredWindow and
// SetLayeredWindowAttributes are mutually exclusive. The renderer, not a global
// flag, is asked: it is the thing that decides how the window gets painted, and
// a layered window nobody ever calls either function on is not shown at all.
//
// The fallback is the 1-bit region mask. Its edges are visibly jagged at 125%
// scaling and above; that is exactly what the layered path exists to fix.
void ApplyCornerTreatment(HWND hwnd, const Renderer& renderer, int widthPx, int heightPx,
                          float radiusPx) {
    if (!hwnd || widthPx <= 0 || heightPx <= 0) return;

    if (renderer.IsLayered()) {
        // A null region removes one. Setting it on a window that has none is a
        // no-op, so this is safe to call on every layout. (GetWindowRgn is not the
        // way to find out first: it copies, it does not hand over ownership.)
        SetWindowRgn(hwnd, nullptr, FALSE);
        return;
    }

    SetLayeredWindowAttributes(hwnd, 0, kSurfaceAlpha, LWA_ALPHA);
    ApplyRoundedRegion(hwnd, widthPx, heightPx, radiusPx);
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

HMONITOR Tab::ResolveMonitor() {
    if (!m_config.monitorId.empty()) {
        if (HMONITOR chosen = DisplayTopology::FindHandle(m_config.monitorId)) {
            if (m_monitorMissing) {
                m_monitorMissing = false;
                Diagnostics::Info("Tab: monitor %ls is back, docking to it again",
                                  m_config.monitorId.c_str());
            }
            return chosen;
        }
        // Not plugged in, or switched off by the Displays tab: not an error. The tab stays
        // reachable on the primary monitor and goes back when this one is on again.
        if (!m_monitorMissing) {
            m_monitorMissing = true;
            Diagnostics::Info("Tab: monitor %ls is not on the desktop, docking to the primary one",
                              m_config.monitorId.c_str());
        }
    } else {
        m_monitorMissing = false;
    }
    return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

bool Tab::ComputeLayout() {
    HMONITOR monitor = ResolveMonitor();
    m_monitor = monitor;

    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfo(monitor, &mi)) return false;
    m_monitorRectPx = mi.rcMonitor;

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    m_dpi = dpiX;
    m_dpiScale = static_cast<float>(dpiX) / 96.0f;

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

    Placement::Input in;
    in.monitor = mi.rcMonitor;
    in.edge = m_config.edge;
    in.tabWidth = tabWpx;
    in.tabHeight = tabHpx;
    in.panelWidth = m_panelWidthPx;
    in.panelHeight = m_panelHeightPx;
    in.verticalRatio = m_config.verticalRatio;
    in.gap = static_cast<int>(std::lround(m_config.panelGap * m_dpiScale));
    const Placement::Result placed = Placement::Compute(in);
    m_tabRectPx = placed.tab;
    m_panelYPx = placed.panelY;
    m_panelOpenXPx = placed.panelOpenX;     // resting position, beside the tab
    m_panelClosedXPx = placed.panelClosedX; // fully outside the edge until it slides in

    // The slide starts outside the monitor. If another monitor is there, the panel would be seen
    // on it for the first frames, so it has to be clipped (ClipSlideTo).
    const RECT closed{m_panelClosedXPx, m_panelYPx, m_panelClosedXPx + m_panelWidthPx,
                      m_panelYPx + m_panelHeightPx};
    m_slideNeedsClip = MonitorFromRect(&closed, MONITOR_DEFAULTTONULL) != nullptr;
    return true;
}

void Tab::ClampPanelY() {
    m_panelYPx = Placement::PanelY(m_tabRectPx, m_panelHeightPx, m_monitorRectPx);
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
    m_tabRenderer.SetCornerRadius(m_config.cornerRadius);
    ApplyCornerTreatment(m_tabHwnd, m_tabRenderer, tabWidth, tabHeight,
                         m_config.cornerRadius * m_dpiScale);

    m_panelHwnd = CreateWindowExW(kWindowExStyle, kPanelClassName, L"EdgeDeck Panel", WS_POPUP,
                                   m_panelClosedXPx, m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                                   nullptr, nullptr, hInstance, this);
    if (!m_panelHwnd) return false;
    if (!m_panelRenderer.AttachToWindow(m_panelHwnd, true)) {
        Diagnostics::Error("Tab: could not create text formats for the panel");
    }
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    m_panelRenderer.SetCornerRadius(m_config.cornerRadius);
    ApplyCornerTreatment(m_panelHwnd, m_panelRenderer, m_panelWidthPx, m_panelHeightPx,
                         m_config.cornerRadius * m_dpiScale);

    // A per-pixel-alpha window shows whatever it was last given and nothing
    // before that, so the tab is painted once before it is made visible rather
    // than trusting a WM_PAINT to arrive for a window that has no content yet.
    m_tabRenderer.DrawTab(m_tabHovered, m_config.tabWidth, m_config.tabHeight,
                          TabGlyphToDraw());
    ShowWindow(m_tabHwnd, SW_SHOWNOACTIVATE);

    // Park the panel just off the edge; it stays hidden until the first hover.
    SetWindowPos(m_panelHwnd, m_tabHwnd, m_panelClosedXPx, m_panelYPx, m_panelWidthPx,
                 m_panelHeightPx, SWP_NOACTIVATE | SWP_HIDEWINDOW);
    RegisterAppBar();
    return true;
}

void Tab::RegisterAppBar() {
    if (m_appBarRegistered || !m_tabHwnd) return;
    APPBARDATA bar{};
    bar.cbSize = sizeof(bar);
    bar.hWnd = m_tabHwnd;
    bar.uCallbackMessage = kAppBarMessage;
    // ABM_NEW only. Without ABM_SETPOS the bar has no edge and reserves no space: nothing on the
    // desktop moves, and the notices still come.
    m_appBarRegistered = SHAppBarMessage(ABM_NEW, &bar) != FALSE;
    if (!m_appBarRegistered) Diagnostics::Error("Tab: could not register for full-screen notices");
}

void Tab::UnregisterAppBar() {
    if (!m_appBarRegistered) return;
    APPBARDATA bar{};
    bar.cbSize = sizeof(bar);
    bar.hWnd = m_tabHwnd;
    SHAppBarMessage(ABM_REMOVE, &bar);
    m_appBarRegistered = false;
}

void Tab::Relayout() {
    if (m_inRelayout || !m_tabHwnd || !m_panelHwnd) return;
    // A move in progress is abandoned, unsaved: the monitor it was measured against may have
    // changed, and the layout below puts the tab back at its saved place.
    if (m_moving) {
        m_moving = false;
        ReleaseCapture();
    }
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
    m_tabRenderer.SetCornerRadius(m_config.cornerRadius);
    m_panelRenderer.SetCornerRadius(m_config.cornerRadius);
    m_tabRenderer.OnResize(static_cast<UINT>(tabWidth), static_cast<UINT>(tabHeight));
    m_panelRenderer.OnResize(static_cast<UINT>(m_panelWidthPx), static_cast<UINT>(m_panelHeightPx));
    ApplyCornerTreatment(m_tabHwnd, m_tabRenderer, tabWidth, tabHeight,
                         m_config.cornerRadius * m_dpiScale);
    ApplyCornerTreatment(m_panelHwnd, m_panelRenderer, m_panelWidthPx, m_panelHeightPx,
                         m_config.cornerRadius * m_dpiScale);
    // On the layered path that call removed any region, the slide clip included.
    m_panelClipped = false;
    ClipSlideTo(panelOpen ? m_panelOpenXPx : m_panelClosedXPx);

    m_tabRenderer.DrawTab(m_tabHovered, m_config.tabWidth, m_config.tabHeight,
                          TabGlyphToDraw());
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

void Tab::RefreshWidget() {
    if (!m_widget) return;
    m_widget->OnSettingsChanged();
    if (!m_tabHwnd || !m_panelHwnd) return;
    // The widget may now want a different height; Relayout recomputes it and repaints an open
    // panel in place.
    Relayout();
    if (m_state == State::Open) DrawPanelSurface();
}

void Tab::SetPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    if (!m_tabHwnd || !m_panelHwnd) return;

    if (paused) {
        // The pause shortcut can come in the middle of a drag; a hidden window must not keep the
        // mouse. A move is kept where it got to, as when the capture is lost any other way.
        EndMove();
        EndDrag();
        KillTimer(m_tabHwnd, kTimerOpen);
        KillTimer(m_tabHwnd, kTimerLeave);
        // A pinned panel is closed too: pausing is "get out of the way", and a panel left on
        // screen is the opposite. No slide: this is a switch, not a gesture.
        if (m_state != State::Hidden) {
            KillTimer(m_tabHwnd, kTimerAnim);
            ShowWindow(m_panelHwnd, SW_HIDE);
            m_state = State::Hidden;
            m_pinned = false;
            if (m_widget) m_widget->OnPanelVisibilityChanged(false);
            UpdateTickTimer();
        }
        m_tabHovered = false;
        ShowWindow(m_tabHwnd, SW_HIDE);
    } else if (!m_fullscreenHidden) {
        // Drawn before it is shown, as at creation: a per-pixel-alpha window shows what it was
        // last given, and the palette may have changed while it was away.
        m_tabRenderer.DrawTab(false, m_config.tabWidth, m_config.tabHeight, TabGlyphToDraw());
        ShowWindow(m_tabHwnd, SW_SHOWNOACTIVATE);
    }
}

bool Tab::SnapshotPanel(std::vector<std::uint8_t>& bgra, UINT& widthPx, UINT& heightPx, bool asUnpinned) {
    if (!m_panelHwnd || !m_widget) return false;
    const bool pinned = m_pinned;
    if (asUnpinned) m_pinned = false;
    DrawPanelSurface();
    m_pinned = pinned;
    return m_panelRenderer.CopyPixels(bgra, widthPx, heightPx);
}

void Tab::SetHiddenForFullscreen(bool hidden) {
    if (m_fullscreenHidden == hidden) return;
    m_fullscreenHidden = hidden;
    if (!m_tabHwnd || !m_panelHwnd) return;

    if (hidden) {
        EndMove();
        EndDrag();
        KillTimer(m_tabHwnd, kTimerOpen);
        // A pinned panel was put there on purpose and stays; one that only opened on a hover
        // closes, without the slide - it is in front of a video or a game.
        if (!m_pinned && m_state != State::Hidden) {
            KillTimer(m_tabHwnd, kTimerAnim);
            ShowWindow(m_panelHwnd, SW_HIDE);
            m_state = State::Hidden;
            if (m_widget) m_widget->OnPanelVisibilityChanged(false);
            UpdateTickTimer();
        }
        m_tabHovered = false;
        ShowWindow(m_tabHwnd, SW_HIDE);
    } else if (!m_paused) {
        m_tabRenderer.DrawTab(false, m_config.tabWidth, m_config.tabHeight, TabGlyphToDraw());
        ShowWindow(m_tabHwnd, SW_SHOWNOACTIVATE);
    }
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
    ApplyCornerTreatment(m_panelHwnd, m_panelRenderer, m_panelWidthPx, m_panelHeightPx,
                         m_config.cornerRadius * m_dpiScale);
    m_panelClipped = false; // the call above removed it; a slide in flight is clipped again now
    ClipSlideTo(CurrentPanelX());
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

void Tab::OnEnter(bool userRequested) {
    KillTimer(m_tabHwnd, kTimerLeave);

    if (m_state == State::Hidden) {
        if (!userRequested) {
            // A pointer that is only passing the edge should not open anything: wait for it to
            // rest on the tab. The timer exists only between arriving on a closed tab and either
            // leaving it or the delay running out, so a quiet EdgeDeck still has no timers. A
            // keyboard shortcut is a request and is never made to wait.
            const int delay = Config::OpenDelayMs();
            if (delay > 0) {
                SetTimer(m_tabHwnd, kTimerOpen, static_cast<UINT>(delay), nullptr);
                return;
            }
        }
        OpenFromHover(userRequested);
    } else if (m_state == State::Closing) {
        // Cursor came back before the close animation finished - reverse it.
        m_animFromX = CurrentPanelX();
        m_animToX = m_panelOpenXPx;
        m_animStartTick = GetTickCount64();
        m_state = State::Opening;
        SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);
    }
}

void Tab::OpenFromHover(bool userRequested) {
    // The tabs live on a screen edge the pointer reaches by accident all the
    // time - in a game, the mouse is pushed against the right edge constantly.
    // A panel opening over the game then is the surprise, so a hover does not
    // open one while a full-screen app is in front of this monitor. Asked at
    // this moment only, so there is nothing to poll, and the keyboard shortcut
    // is a request, not a hover, and always goes through.
    if (!userRequested && Config::FullscreenGuard() && Fullscreen::AppOnMonitor(m_monitor)) return;
    BeginOpen();
}

void Tab::OnLeave() {
    // Left before the open delay was up: nothing was going to open.
    KillTimer(m_tabHwnd, kTimerOpen);
    // Debounce: don't close immediately, the cursor may just be crossing the
    // (borderless) seam between the tab window and the panel window.
    SetTimer(m_tabHwnd, kTimerLeave, static_cast<UINT>(m_config.closeDelayMs), nullptr);
}

void Tab::CheckPendingClose() {
    POINT pt;
    GetCursorPos(&pt);
    if (IsPointInside(pt) || m_pinned || m_dragging || m_moving) return;
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
        // A widget that reads its data right here (Volume lists its devices) has its height now;
        // only an asynchronous answer used to resize the panel, so Volume opened as tall as its
        // "Checking audio..." line and showed one device of six.
        ResizePanelToContent();
    }

    m_animFromX = m_panelClosedXPx;
    m_animToX = m_panelOpenXPx;
    m_animStartTick = GetTickCount64();
    m_state = State::Opening;
    m_panelPaintedOnScreen = false; // see StepAnimation
    SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);

    // Parked outside the edge, the panel is shown a moment before the first animation tick moves
    // it; with a monitor beyond that edge it would flash on that monitor. Clip it first.
    ClipSlideTo(m_animFromX);

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
    OnEnter(true);
    // A keyboard-opened panel has no cursor over it, so nothing would ever start
    // the leave countdown. Pin it instead, and let Escape close it.
    if (m_state != State::Hidden) {
        m_pinned = true;
        InvalidateRect(m_panelHwnd, nullptr, FALSE);
    }
}

const wchar_t* Tab::TabGlyphToDraw() const {
    if (!m_widget) return L"?";
    // The icon when the system icon font is there, the widget's plain text glyph
    // when it is not (or when the widget has no icon).
    const wchar_t* icon = m_widget->TabIcon();
    return (icon && Renderer::IconFontAvailable()) ? icon : m_widget->TabGlyph();
}

void Tab::ActivateShortcut(int controlId) {
    // The tab window, not the panel, is the widget's owner for async replies,
    // which is also what a click on the panel passes.
    if (!m_widget || !m_tabHwnd) return;
    m_widget->OnShortcut(controlId, m_tabHwnd);
    if (m_panelHwnd && IsWindowVisible(m_panelHwnd)) InvalidateRect(m_panelHwnd, nullptr, FALSE);
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

void Tab::BeginMove(int cursorScreenY) {
    m_moving = true;
    m_moveStartCursorY = cursorScreenY;
    m_moveStartTabTop = m_tabRectPx.top;
    SetCapture(m_panelHwnd);
    InvalidateRect(m_panelHwnd, nullptr, FALSE);
}

void Tab::MoveWith(int cursorScreenY) {
    const int tabHeight = m_tabRectPx.bottom - m_tabRectPx.top;
    int wanted = m_moveStartTabTop + (cursorScreenY - m_moveStartCursorY);
    // The handle sits below the panel's top and the panel stays on the monitor, so followed one to
    // one the pointer meets the screen's edge with the tab still short of it - 30-odd pixels on a
    // monitor with nothing above or below. Pushed against the monitor's edge, the tab goes all the
    // way.
    if (cursorScreenY <= m_monitorRectPx.top) wanted = m_monitorRectPx.top;
    if (cursorScreenY >= m_monitorRectPx.bottom - 1) wanted = m_monitorRectPx.bottom - tabHeight;
    const int top = Placement::ClampTabTop(wanted, tabHeight, m_monitorRectPx);
    if (top == m_tabRectPx.top) return; // pressed against the top or bottom: nothing to do
    OffsetRect(&m_tabRectPx, 0, top - m_tabRectPx.top);
    // The panel stays centred on the tab, as it is placed, and stops at the monitor's edges.
    m_panelYPx = Placement::PanelY(m_tabRectPx, m_panelHeightPx, m_monitorRectPx);

    // Both in one go, so the two never part for a frame. Only positions change: no size, no
    // z-order, no paint.
    const UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    HDWP batch = BeginDeferWindowPos(2);
    if (batch) batch = DeferWindowPos(batch, m_tabHwnd, nullptr, m_tabRectPx.left, m_tabRectPx.top, 0, 0, flags);
    if (batch) batch = DeferWindowPos(batch, m_panelHwnd, nullptr, CurrentPanelX(), m_panelYPx, 0, 0, flags);
    if (!batch || !EndDeferWindowPos(batch)) {
        SetWindowPos(m_tabHwnd, nullptr, m_tabRectPx.left, m_tabRectPx.top, 0, 0, flags);
        SetWindowPos(m_panelHwnd, nullptr, CurrentPanelX(), m_panelYPx, 0, 0, flags);
    }
}

void Tab::EndMove() {
    if (!m_moving) return;
    m_moving = false; // first, so WM_CAPTURECHANGED from ReleaseCapture is a no-op
    ReleaseCapture();
    if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);

    if (m_tabRectPx.top != m_moveStartTabTop) {
        const float before = m_config.verticalRatio;
        m_config.verticalRatio = Placement::RatioForTabTop(m_tabRectPx.top, m_tabRectPx.bottom - m_tabRectPx.top,
                                                           m_monitorRectPx);
        if (m_owner) m_owner->OnTabMoved(this, before, m_config.verticalRatio);
    }

    // As after a slider drag: the leave events went to the capture, so if the pointer is outside
    // now, the usual close countdown starts.
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

void Tab::ClipSlideTo(int x) {
    // The rounded-region fallback (no per-pixel alpha) already owns the window region; replacing
    // it would square the corners, and that path is the rare one. There the slide is not clipped.
    if (!m_panelHwnd || !m_panelRenderer.IsLayered()) return;

    auto removeClip = [&] {
        if (!m_panelClipped) return;
        SetWindowRgn(m_panelHwnd, nullptr, FALSE);
        m_panelClipped = false;
    };

    if (!m_slideNeedsClip) {
        removeClip(); // the layout changed and no monitor is beyond the edge any more
        return;
    }

    const RECT shown =
        Placement::VisiblePart(x, m_panelYPx, m_panelWidthPx, m_panelHeightPx, m_monitorRectPx);
    const bool whole = shown.left == 0 && shown.top == 0 && shown.right == m_panelWidthPx &&
                       shown.bottom == m_panelHeightPx;
    if (whole) {
        removeClip(); // fully on its own monitor: nothing to cut
        return;
    }
    // An empty rectangle is a valid, empty region: nothing of the panel shows yet.
    HRGN region = CreateRectRgn(shown.left, shown.top, shown.right, shown.bottom);
    if (!region) return;
    if (SetWindowRgn(m_panelHwnd, region, FALSE)) {
        m_panelClipped = true; // the window owns the region now
    } else {
        DeleteObject(region);
    }
}

void Tab::MovePanelTo(int x) {
    if (!m_panelHwnd || !IsWindowVisible(m_panelHwnd)) return;
    // The window and its clip cannot change in the same instant, and the frame between the two
    // calls must never show the panel on the neighbouring monitor. Moving in, the new (larger)
    // clip applied to the old position would let a sliver out, so the move goes first and the
    // old, tighter clip holds meanwhile. Moving out it is the other way round: the new, tighter
    // clip is set first and the window then follows it.
    const bool movingIn = m_state == State::Opening;
    if (!movingIn) ClipSlideTo(x);
    // Reposition only (SWP_NOSIZE): a slide costs one cheap call per frame
    // instead of a full relayout of a topmost layered window.
    SetWindowPos(m_panelHwnd, nullptr, x, m_panelYPx, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (movingIn) ClipSlideTo(x);
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

    // DWM keeps no picture of a per-pixel-alpha window that was presented while it lay wholly
    // outside the desktop, and a slide starts exactly there, just beyond the edge. The paint that
    // BeginOpen asks for was therefore thrown away, and the panel stayed invisible - the slide
    // included - until something repainted it, in practice the pointer entering it. So it is
    // painted again the first time it touches its monitor.
    if (!m_panelPaintedOnScreen && m_state == State::Opening) {
        const RECT shown = Placement::VisiblePart(x, m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                                                  m_monitorRectPx);
        if (shown.right > shown.left) {
            m_panelPaintedOnScreen = true;
            DrawPanelSurface();
        }
    }

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

void Tab::ShowNotice(const std::wstring& text) {
    if (text.empty()) return;

    const bool panelOnScreen = m_panelHwnd && (m_state == State::Open || m_state == State::Opening);
    if (!panelOnScreen) {
        // A shortcut, or a panel that closed before the reply arrived: there is no
        // header to put it in, and silence would look like nothing happened.
        if (m_owner) m_owner->ShowBalloon(text.c_str());
        return;
    }

    m_notice = text;
    // SetTimer on a timer that is already pending restarts it, so a second
    // problem gets its full time rather than inheriting what was left.
    SetTimer(m_tabHwnd, kTimerNotice, kNoticeMs, nullptr);
    DrawPanelSurface();
}

void Tab::DrawPanelSurface() {
    if (!m_panelHwnd || !m_widget) return;
    m_panelRenderer.DrawPanel(m_config.panelWidth, m_panelHeightLogical, m_widget.get(), m_pinned,
                              m_hoveredControl == kPinControlId, m_hoveredControl == kMoveControlId, m_moving,
                              m_notice.empty() ? nullptr : m_notice.c_str());
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
    const D2D1_RECT_F moveRect = PanelLayout::MoveHandleRect();
    if (x >= moveRect.left && x <= moveRect.right && y >= moveRect.top && y <= moveRect.bottom) {
        return kMoveControlId;
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
            // Monitors were added, removed or rearranged. The tab docks to its
            // configured monitor, or to the primary one, and either can change under
            // it - the Displays tab does exactly that when it switches to the TV
            // alone - so dock again: Relayout looks the monitor up afresh, which is
            // also what puts a tab back on a monitor that has just been switched on.
            //
            // An open panel that is not pinned is closed first. Relayout cancels
            // its hover tracking, so a panel left open here would never see the
            // cursor leave and would sit on screen until the next hover.
            if (!m_pinned) BeginClose();
            PanelTheme::Refresh();
            Relayout();
            return 0;
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
                                  TabGlyphToDraw());
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
            if (wParam == kTimerOpen) {
                // The pointer has rested on the tab for the open delay. Asked again whether it
                // is still there: a tab can be left and re-entered, or paused, in that time.
                KillTimer(hwnd, kTimerOpen);
                POINT pt;
                GetCursorPos(&pt);
                if (!m_paused && m_state == State::Hidden && IsPointInside(pt)) OpenFromHover(false);
                return 0;
            }
            if (wParam == kTimerTick) {
                if (m_widget && (m_state == State::Open || m_state == State::Opening)) {
                    m_widget->OnTick();
                    ResizePanelToContent(); // a no-op unless the content's height changed
                    if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (wParam == kTimerNotice) {
                KillTimer(hwnd, kTimerNotice);
                m_notice.clear();
                if (m_panelHwnd && IsWindowVisible(m_panelHwnd)) InvalidateRect(m_panelHwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case PanelWidget::kNoticeMessage: {
            // The envelope is ours to free whatever it holds, so it is deleted on
            // every path.
            auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
            if (!envelope) return 0;
            std::wstring text;
            if (envelope->Kind() == AsyncKind::Notice) {
                text = static_cast<NoticeEnvelope*>(envelope)->text;
            }
            delete envelope;
            ShowNotice(text);
            return 0;
        }
        case kAppBarMessage:
            if (wParam == ABN_FULLSCREENAPP && m_owner) m_owner->OnFullscreenNotice();
            return 0;
        case WM_DESTROY: {
            UnregisterAppBar();
            KillTimer(hwnd, kTimerAnim);
            KillTimer(hwnd, kTimerLeave);
            KillTimer(hwnd, kTimerTick);
            KillTimer(hwnd, kTimerNotice);
            KillTimer(hwnd, kTimerOpen);
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
            if (m_moving) {
                // In screen coordinates, as of this message: lParam is relative to a window that
                // this very move keeps moving.
                MoveWith(GET_Y_LPARAM(GetMessagePos()));
                return 0;
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
            if (HitTestPanel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) == kMoveControlId) {
                BeginMove(GET_Y_LPARAM(GetMessagePos()));
                return 0;
            }
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
            // Capture taken away mid-move (another window grabbed it): the tab stays where it is
            // on screen, so that is what is saved.
            if (m_moving && reinterpret_cast<HWND>(lParam) != hwnd) EndMove();
            if (m_dragging && reinterpret_cast<HWND>(lParam) != hwnd) EndDrag();
            return 0;
        case WM_SETCURSOR:
            // Up-and-down arrows over the handle: it moves the tab along its edge, nowhere else.
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (m_moving || HitTestPanel(pt.x, pt.y) == kMoveControlId) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONUP: {
            if (m_moving) {
                EndMove();
                return 0;
            }
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
                ResizePanelToContent();
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

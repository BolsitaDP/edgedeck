#include "Tab.h"
#include "App.h"

#include <shellscalingapi.h>
#include <windowsx.h>
#include <cmath>
#include <algorithm>

namespace {
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
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint the whole client area ourselves

    wc.lpszClassName = kTabClassName;
    wc.lpfnWndProc = TabProc;
    if (!RegisterClassExW(&wc)) return false;

    wc.lpszClassName = kPanelClassName;
    wc.lpfnWndProc = PanelProc;
    return RegisterClassExW(&wc) != 0;
}

bool Tab::Create(HINSTANCE hInstance) {
    return ComputeLayout() && CreateWindows(hInstance);
}

bool Tab::ComputeLayout() {
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

    m_panelYPx = tabY + (tabHpx - m_panelHeightPx) / 2;
    m_panelYPx = std::clamp(m_panelYPx, monTop, static_cast<int>(monBottom) - m_panelHeightPx);

    m_panelOpenXPx = tabX - m_panelWidthPx; // resting position, left of the tab
    m_panelClosedXPx = monRight;            // fully outside the edge until it slides in
    return true;
}

bool Tab::CreateWindows(HINSTANCE hInstance) {
    // Both windows are permanently WS_EX_NOACTIVATE: no interaction with a
    // tab - hover, click, or pin - ever steals foreground focus.
    const DWORD exStyle =
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;

    m_tabHwnd = CreateWindowExW(
        exStyle, kTabClassName, L"EdgeDeck", WS_POPUP, m_tabRectPx.left, m_tabRectPx.top,
        m_tabRectPx.right - m_tabRectPx.left, m_tabRectPx.bottom - m_tabRectPx.top, nullptr,
        nullptr, hInstance, this);
    if (!m_tabHwnd) return false;

    if (!SetLayeredWindowAttributes(m_tabHwnd, 0, m_config.windowAlpha, LWA_ALPHA) ||
        !ApplyRoundedRegion(m_tabHwnd, m_tabRectPx.right - m_tabRectPx.left,
                            m_tabRectPx.bottom - m_tabRectPx.top,
                            m_config.cornerRadius * m_dpiScale) ||
        !m_tabRenderer.AttachToWindow(m_tabHwnd)) return false;
    m_tabRenderer.SetRenderDpi(static_cast<float>(m_dpi));

    m_panelHwnd = CreateWindowExW(exStyle, kPanelClassName, L"EdgeDeck Panel", WS_POPUP,
                                   m_panelClosedXPx, m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                                   nullptr, nullptr, hInstance, this);
    if (!m_panelHwnd) return false;

    if (!SetLayeredWindowAttributes(m_panelHwnd, 0, m_config.windowAlpha, LWA_ALPHA) ||
        !ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                            m_config.cornerRadius * m_dpiScale) ||
        !m_panelRenderer.AttachToWindow(m_panelHwnd)) return false;
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));

    ShowWindow(m_tabHwnd, SW_SHOWNOACTIVATE);
    return true;
}

void Tab::Relayout() {
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
    m_hoveredControl = -1;
    if (m_widget) m_widget->SetHovered(-1);

    TRACKMOUSEEVENT tabLeave{sizeof(tabLeave), TME_CANCEL | TME_LEAVE, m_tabHwnd, 0};
    TRACKMOUSEEVENT panelLeave{sizeof(panelLeave), TME_CANCEL | TME_LEAVE, m_panelHwnd, 0};
    TrackMouseEvent(&tabLeave);
    TrackMouseEvent(&panelLeave);
    m_tabTracking = false;
    m_panelTracking = false;
    m_tabHovered = false;

    const int tabWidth = m_tabRectPx.right - m_tabRectPx.left;
    const int tabHeight = m_tabRectPx.bottom - m_tabRectPx.top;
    SetWindowPos(m_tabHwnd, HWND_TOPMOST, m_tabRectPx.left, m_tabRectPx.top, tabWidth, tabHeight,
                 SWP_NOACTIVATE);
    SetWindowPos(m_panelHwnd, m_tabHwnd, panelOpen ? m_panelOpenXPx : m_panelClosedXPx, m_panelYPx,
                 m_panelWidthPx, m_panelHeightPx,
                 SWP_NOACTIVATE | (panelOpen ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

    ApplyRoundedRegion(m_tabHwnd, tabWidth, tabHeight, m_config.cornerRadius * m_dpiScale);
    ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                        m_config.cornerRadius * m_dpiScale);
    m_tabRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    m_panelRenderer.SetRenderDpi(static_cast<float>(m_dpi));
    m_tabRenderer.OnResize(static_cast<UINT>(tabWidth), static_cast<UINT>(tabHeight));
    m_panelRenderer.OnResize(static_cast<UINT>(m_panelWidthPx), static_cast<UINT>(m_panelHeightPx));
    InvalidateRect(m_tabHwnd, nullptr, FALSE);
    InvalidateRect(m_panelHwnd, nullptr, FALSE);
    m_inRelayout = false;
}

void Tab::ResizePanelToContent() {
    if (!m_widget || !m_panelHwnd) return;

    float newHeightLogical = PanelLayout::ChromeHeight +
                              m_widget->PreferredContentHeight(m_config.panelWidth) +
                              PanelLayout::BottomPadding;
    int newHeightPx = static_cast<int>(std::lround(newHeightLogical * m_dpiScale));
    if (newHeightPx == m_panelHeightPx) return;

    m_panelHeightLogical = newHeightLogical;
    m_panelHeightPx = newHeightPx;

    // Keep the panel centered on the tab as it grows/shrinks, but never
    // touch X - the slide animation may still be mid-flight and this must
    // not snap it to its resting position early.
    int tabCenterY = (m_tabRectPx.top + m_tabRectPx.bottom) / 2;
    m_panelYPx = tabCenterY - m_panelHeightPx / 2;
    int currentX = CurrentPanelX();

    SetWindowPos(m_panelHwnd, nullptr, currentX, m_panelYPx, m_panelWidthPx, m_panelHeightPx,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyRoundedRegion(m_panelHwnd, m_panelWidthPx, m_panelHeightPx,
                        m_config.cornerRadius * m_dpiScale);
    m_panelRenderer.OnResize(static_cast<UINT>(m_panelWidthPx), static_cast<UINT>(m_panelHeightPx));
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
        InvalidateRect(m_tabHwnd, nullptr, FALSE);
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

    if (m_state == State::Open || m_state == State::Opening) {
        BeginClose();
    }
}

bool Tab::IsPointInside(POINT screenPt) const {
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
    SetWindowPos(m_panelHwnd, m_tabHwnd, m_panelClosedXPx, m_panelYPx, m_panelWidthPx,
                 m_panelHeightPx, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    m_animFromX = m_panelClosedXPx;
    m_animToX = m_panelOpenXPx;
    m_animStartTick = GetTickCount64();
    m_state = State::Opening;
    SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);

    InvalidateRect(m_tabHwnd, nullptr, FALSE);
}

void Tab::BeginClose() {
    m_animFromX = CurrentPanelX();
    m_animToX = m_panelClosedXPx;
    m_animStartTick = GetTickCount64();
    m_state = State::Closing;
    SetTimer(m_tabHwnd, kTimerAnim, kAnimIntervalMs, nullptr);

    InvalidateRect(m_tabHwnd, nullptr, FALSE);
}

void Tab::TogglePin() {
    m_pinned = !m_pinned;
    InvalidateRect(m_panelHwnd, nullptr, FALSE);

    if (!m_pinned) {
        // Unpinned via the button itself; if the cursor already left both
        // windows, close now instead of waiting for a leave event that
        // already happened while the panel was pinned open.
        POINT pt;
        GetCursorPos(&pt);
        if (!IsPointInside(pt) && (m_state == State::Open || m_state == State::Opening)) {
            BeginClose();
        }
    }
}

void Tab::EndDrag() {
    if (!m_dragging) return;
    m_dragging = false; // first, so the WM_CAPTURECHANGED from ReleaseCapture is a no-op
    ReleaseCapture();
    if (m_widget) m_widget->OnDragEnd();
    if (m_panelHwnd) InvalidateRect(m_panelHwnd, nullptr, FALSE);

    // The cursor may have left the panel while dragging; the leave events
    // were swallowed by the capture, so start the normal close countdown.
    POINT pt;
    GetCursorPos(&pt);
    if (!IsPointInside(pt)) OnLeave();
}

void Tab::StepAnimation() {
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

int Tab::CurrentPanelX() const {
    RECT r{};
    GetWindowRect(m_panelHwnd, &r);
    return r.left;
}

// ---------------------------------------------------------------------------
// Panel hit-testing (chrome pin button, then delegate to the widget)
// ---------------------------------------------------------------------------

int Tab::HitTestPanel(int clientXPx, int clientYPx) const {
    float x = static_cast<float>(clientXPx) / m_dpiScale;
    float y = static_cast<float>(clientYPx) / m_dpiScale;

    D2D1_RECT_F pinRect = PanelLayout::PinButtonRect(m_config.panelWidth);
    if (x >= pinRect.left && x <= pinRect.right && y >= pinRect.top && y <= pinRect.bottom) {
        return kPinControlId;
    }

    if (y < PanelLayout::ChromeHeight || !m_widget) return -1;

    float contentHeight = m_panelHeightLogical - PanelLayout::ChromeHeight - PanelLayout::BottomPadding;
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
            Relayout();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            const wchar_t* glyph = m_widget ? m_widget->TabGlyph() : L"?";
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
        case WM_DESTROY: {
            KillTimer(hwnd, kTimerAnim);
            KillTimer(hwnd, kTimerLeave);
            if (m_panelHwnd) {
                DestroyWindow(m_panelHwnd);
                m_panelHwnd = nullptr;
            }
            return 0;
        }
        default:
            if (msg >= WM_APP && m_widget) {
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
                float x = static_cast<float>(GET_X_LPARAM(lParam)) / m_dpiScale;
                float y = static_cast<float>(GET_Y_LPARAM(lParam)) / m_dpiScale -
                          PanelLayout::ChromeHeight;
                float contentHeight = m_panelHeightLogical - PanelLayout::ChromeHeight -
                                      PanelLayout::BottomPadding;
                m_widget->OnDragMove(x, y, m_config.panelWidth, contentHeight);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int hit = HitTestPanel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (hit != m_hoveredControl) {
                m_hoveredControl = hit;
                if (m_widget) m_widget->SetHovered(hit >= 0 ? hit : -1);
                InvalidateRect(hwnd, nullptr, FALSE);
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
            float x = static_cast<float>(GET_X_LPARAM(lParam)) / m_dpiScale;
            float y = static_cast<float>(GET_Y_LPARAM(lParam)) / m_dpiScale -
                      PanelLayout::ChromeHeight;
            if (y < 0.0f) return 0; // press on the title/pin chrome, not the content
            float contentHeight = m_panelHeightLogical - PanelLayout::ChromeHeight -
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
            int hit = HitTestPanel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
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
        case WM_ERASEBKGND:
            return 1;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            Relayout();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            m_panelRenderer.DrawPanel(m_config.panelWidth, m_panelHeightLogical, m_widget.get(),
                                       m_pinned, m_hoveredControl == kPinControlId);
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

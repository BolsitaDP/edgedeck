#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "PanelWidget.h"
#include "Placement.h"
#include "Renderer.h"

class App;

// Per-tab tunable geometry. Logical units at 96 DPI; Tab scales them to
// physical pixels for whichever monitor it lands on. verticalRatio positions
// the tab independently of every other tab, so several can coexist without
// overlapping.
struct TabConfig {
    float verticalRatio = 0.5f; // 0 = top of monitor, 1 = bottom
    ScreenEdge edge = ScreenEdge::Right;
    // EDID id of the monitor to dock to; empty is the primary monitor. If that monitor is not on
    // the desktop the tab docks to the primary one until it is (see ResolveMonitor).
    std::wstring monitorId;
    float tabWidth = 26.0f;
    float tabHeight = 76.0f;
    float panelWidth = 300.0f;
    float cornerRadius = 10.0f;
    int animationMs = 200;
    int closeDelayMs = 320;
};

// One edge tab + its flyout panel. Owns its own hover/animate state machine
// and a single PanelWidget that supplies the panel's content. Both windows
// are WS_EX_NOACTIVATE unconditionally - interacting with a tab never steals
// focus from whatever the user was using, pinned or not.
class Tab {
public:
    Tab(App* owner, TabConfig config, std::unique_ptr<PanelWidget> widget);
    ~Tab();

    Tab(const Tab&) = delete;
    Tab& operator=(const Tab&) = delete;

    // Registers both window classes. Must be called exactly once, before any
    // Tab is constructed.
    static bool RegisterClasses(HINSTANCE hInstance);
    static void UnregisterClasses();

    bool Create(HINSTANCE hInstance);

    // True when the cursor is over this tab or its (visible) panel.
    bool IsPointInside(POINT screenPt) const;

    // Which widget this tab hosts. App uses it to decide whether a settings
    // change can be applied in place or needs the tab rebuilt.
    WidgetType WidgetType() const { return m_widget ? m_widget->Type() : WidgetType::QuickActions; }

    // Opens and pins the panel, for a keyboard shortcut that named this tab.
    void RequestOpen();

    // Hands a keyboard shortcut for one of the widget's own controls to the
    // widget, without opening the panel.
    void ActivateShortcut(int controlId);

    // Re-lays out for a new configuration without replacing the widget, so an
    // open or pinned panel and any in-flight async work survive the change.
    void ApplyConfig(const TabConfig& config);

private:
    enum class State { Hidden, Opening, Open, Closing };

    static LRESULT CALLBACK TabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleTabMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandlePanelMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool ComputeLayout();
    // The monitor the tab is docked to: the configured one if it is on the desktop, otherwise the
    // primary one. Looked up afresh on every layout, so a monitor that is switched back on takes
    // its tabs back at the next display change.
    HMONITOR ResolveMonitor();
    bool CreateWindows(HINSTANCE hInstance);
    void Relayout();
    // Re-sizes the panel window to match the widget's current
    // PreferredContentHeight (e.g. MediaWidget's row count changed after an
    // async refresh). No-op if the height hasn't actually changed.
    void ResizePanelToContent();

    // userRequested: the open was asked for outright (a keyboard shortcut), not
    // implied by the pointer passing over the tab. Only an implied open is held
    // back by the full-screen guard.
    void OnEnter(bool userRequested = false);
    void OnLeave();
    void BeginOpen();
    void BeginClose();
    void StepAnimation();
    void CheckPendingClose();
    void TogglePin();
    void EndDrag();
    int CurrentPanelX() const;

    void UpdateTickTimer();

    // What the edge tab shows: the widget's icon if the icon font exists, else its glyph.
    const wchar_t* TabGlyphToDraw() const;

    // Shows a widget's problem report: in the panel's header while the panel is
    // on screen, otherwise as a tray notification (nobody is looking at the tab).
    void ShowNotice(const std::wstring& text);
    void MovePanelTo(int x);
    // While the panel is still outside its edge it is on whatever monitor lies beyond - with a
    // monitor there, the slide would be seen emerging from it. A window region cuts the panel to
    // the monitor it belongs to for the duration of the slide. A no-op (and no region) when no
    // other monitor is beyond this edge, which is the common case.
    void ClipSlideTo(int x);
    void ClampPanelY();
    void DrawPanelSurface();

    // Chrome (title/pin) hit-test; delegates to the widget for anything
    // below the chrome. Returns a control id: kPinControlId, or a
    // widget-defined id (>=0), or -1 for no hit.
    int HitTestPanel(int clientXPx, int clientYPx) const;

    TabConfig m_config;
    std::unique_ptr<PanelWidget> m_widget;
    App* m_owner = nullptr;

    HWND m_tabHwnd = nullptr;
    HWND m_panelHwnd = nullptr;

    Renderer m_tabRenderer;
    Renderer m_panelRenderer;

    State m_state = State::Hidden;
    bool m_tabTracking = false;
    bool m_panelTracking = false;
    bool m_tabHovered = false;
    bool m_pinned = false;
    bool m_dragging = false; // a widget slider owns the mouse (SetCapture) until button-up
    bool m_inRelayout = false;
    int m_hoveredControl = -1;
    int m_focusedControl = -1;

    float m_dpiScale = 1.0f;
    UINT m_dpi = 96;
    HMONITOR m_monitor = nullptr;
    RECT m_monitorRectPx{};     // the monitor above, as of the last layout
    bool m_monitorMissing = false; // the configured monitor is not on the desktop (logged once)
    bool m_slideNeedsClip = false; // another monitor lies beyond the edge the panel slides from
    bool m_panelClipped = false;   // the panel window currently carries a slide-clip region

    // Physical-pixel geometry, in screen coordinates.
    RECT m_tabRectPx{};
    int m_panelOpenXPx = 0;
    int m_panelClosedXPx = 0;
    int m_panelYPx = 0;
    int m_panelWidthPx = 0;
    int m_panelHeightPx = 0;
    float m_panelHeightLogical = 0.0f;

    // The notice currently replacing the panel title; empty when there is none.
    std::wstring m_notice;

    ULONGLONG m_animStartTick = 0;
    int m_animFromX = 0;
    int m_animToX = 0;

    static constexpr UINT_PTR kTimerAnim = 1;
    static constexpr UINT_PTR kTimerLeave = 2;
    static constexpr UINT_PTR kTimerTick = 3;
    static constexpr UINT_PTR kTimerNotice = 4;
    static constexpr UINT kAnimIntervalMs = 15;

    // How long a notice stays in the header. One-shot, and only armed while a
    // notice is showing, so a quiet EdgeDeck still has no timers at all.
    static constexpr UINT kNoticeMs = 6000;

    // How often a visible panel samples the one thing Windows offers no event
    // for: the playback position inside a track. Runs only while a panel is
    // open and only for a widget that asked for it, so a closed EdgeDeck still
    // has no timers at all.
    static constexpr UINT kTickIntervalMs = 500;

    static constexpr int kPinControlId = -2; // distinct from "no hit" (-1) and widget ids (>=0)
};

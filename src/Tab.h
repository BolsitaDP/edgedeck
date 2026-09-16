#pragma once

#include <windows.h>
#include <memory>
#include "PanelWidget.h"
#include "Renderer.h"

class App;

// Per-tab tunable geometry. Logical units at 96 DPI; Tab scales them to
// physical pixels for whichever monitor it lands on. verticalRatio positions
// the tab independently of every other tab, so several can coexist without
// overlapping.
struct TabConfig {
    float verticalRatio = 0.5f; // 0 = top of monitor, 1 = bottom
    float tabWidth = 26.0f;
    float tabHeight = 76.0f;
    float panelWidth = 300.0f;
    float cornerRadius = 10.0f;
    int animationMs = 200;
    int closeDelayMs = 320;
    BYTE windowAlpha = 235; // out of 255; whole-window constant alpha via LWA_ALPHA
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

    bool Create(HINSTANCE hInstance);

    bool IsPinned() const { return m_pinned; }
    // Called by App's mouse hook when a click lands outside this tab while pinned.
    void ClosePinned();
    // Used by App's mouse hook and by this tab's own close-delay check.
    bool IsPointInside(POINT screenPt) const;

private:
    enum class State { Hidden, Opening, Open, Closing };

    static LRESULT CALLBACK TabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleTabMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandlePanelMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool ComputeLayout();
    bool CreateWindows(HINSTANCE hInstance);
    void Relayout();
    // Re-sizes the panel window to match the widget's current
    // PreferredContentHeight (e.g. MediaWidget's row count changed after an
    // async refresh). No-op if the height hasn't actually changed.
    void ResizePanelToContent();

    void OnEnter();
    void OnLeave();
    void BeginOpen();
    void BeginClose();
    void StepAnimation();
    void CheckPendingClose();
    void TogglePin();
    int CurrentPanelX() const;

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
    bool m_inRelayout = false;
    int m_hoveredControl = -1;

    float m_dpiScale = 1.0f;
    UINT m_dpi = 96;

    // Physical-pixel geometry, in screen coordinates.
    RECT m_tabRectPx{};
    int m_panelOpenXPx = 0;
    int m_panelClosedXPx = 0;
    int m_panelYPx = 0;
    int m_panelWidthPx = 0;
    int m_panelHeightPx = 0;
    float m_panelHeightLogical = 0.0f;

    ULONGLONG m_animStartTick = 0;
    int m_animFromX = 0;
    int m_animToX = 0;

    static constexpr UINT_PTR kTimerAnim = 1;
    static constexpr UINT_PTR kTimerLeave = 2;
    static constexpr UINT kAnimIntervalMs = 15;
    static constexpr int kPinControlId = -2; // distinct from "no hit" (-1) and widget ids (>=0)
};

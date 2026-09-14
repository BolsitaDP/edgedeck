#pragma once

#include <windows.h>
#include "Renderer.h"
#include <vector>

// Which physical edge of the monitor EdgeDeck is docked to. Only Right is
// wired up in this MVP; the enum exists so ComputeLayout() already has a
// place to branch from when left-edge support is added later.
enum class Edge {
    Left,
    Right,
};

// Tunable geometry/timing. All sizes are logical units at 96 DPI; EdgeWindow
// scales them to physical pixels for the monitor it lands on.
struct EdgeDeckConfig {
    Edge edge = Edge::Right;
    float verticalRatio = 0.5f; // 0 = top of monitor, 1 = bottom
    float tabWidth = 26.0f;
    float tabHeight = 76.0f;
    float panelWidth = 300.0f;
    float panelHeight = 0.0f; // computed from content in EdgeWindow::Create
    float cornerRadius = 10.0f;
    int animationMs = 200;
    int closeDelayMs = 320;
    BYTE windowAlpha = 235; // out of 255; whole-window constant alpha via LWA_ALPHA
};

// Owns the two always-on-top popup windows (edge tab + flyout panel) and all
// the state needed to drive them: hover tracking, the slide animation, and
// the handful of native actions the panel can trigger. Deliberately a
// singleton for this MVP - see s_instance.
class EdgeWindow {
public:
    bool Create(HINSTANCE hInstance);

    static int RunMessageLoop();

private:
    enum class State {
        Hidden,
        Opening,
        Open,
        Closing,
    };

    static LRESULT CALLBACK TabProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LRESULT HandleTabMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandlePanelMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void RegisterClasses(HINSTANCE hInstance);
    void ComputeLayout();
    void CreateWindows(HINSTANCE hInstance);

    void OnEnter();
    void OnLeave();
    void BeginOpen();
    void BeginClose();
    void StepAnimation();
    void CheckPendingClose();
    int CurrentPanelX() const;

    int RowAt(int clientYPx) const;
    void InvokeRow(int index);

    void ShowContextMenu(POINT screenPt);

    EdgeDeckConfig m_config;

    HWND m_tabHwnd = nullptr;
    HWND m_panelHwnd = nullptr;

    Renderer m_tabRenderer;
    Renderer m_panelRenderer;
    std::vector<PanelItem> m_items;

    State m_state = State::Hidden;

    bool m_tabTracking = false;
    bool m_panelTracking = false;
    bool m_tabHovered = false;
    int m_hoveredRow = -1;

    float m_dpiScale = 1.0f;
    UINT m_dpi = 96;

    // Physical-pixel geometry, in screen coordinates.
    RECT m_tabRectPx{};
    int m_panelOpenXPx = 0;
    int m_panelClosedXPx = 0;
    int m_panelYPx = 0;
    int m_panelWidthPx = 0;
    int m_panelHeightPx = 0;

    // Animation state (position-only slide; panel window size never changes).
    ULONGLONG m_animStartTick = 0;
    int m_animFromX = 0;
    int m_animToX = 0;

    static constexpr UINT_PTR kTimerAnim = 1;
    static constexpr UINT_PTR kTimerLeave = 2;
    static constexpr int kHotkeyId = 1;
    static constexpr UINT kAnimIntervalMs = 15;

    static EdgeWindow* s_instance;
};

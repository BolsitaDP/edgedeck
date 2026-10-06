#pragma once

#include <windows.h>
#include <functional>
#include <vector>
#include "Config.h"

// Modeless comctl32 settings dialog: lets the user pick each tab's widget
// type, vertical position, and size, then reorder/add/remove tabs. Built
// with plain Win32 common controls (not hand-rolled D2D) since this is a
// rarely-opened, low-traffic secondary window - it costs nothing while
// closed and comctl32 gives keyboard navigation for free via IsDialogMessage
// in App's message loop.
class SettingsWindow {
public:
    using SaveCallback = std::function<void(const std::vector<TabSettings>&)>;
    // Something the window itself could not do (changing the startup entry), said
    // without a dialog: the window is closing as it is reported, so the owner shows
    // it somewhere that outlives it. Optional.
    using ProblemCallback = std::function<void(const wchar_t*)>;

    bool Create(HINSTANCE hInstance, std::vector<TabSettings> initial, SaveCallback onSave,
                ProblemCallback onProblem = {});
    HWND Hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls(HINSTANCE hInstance);
    void RefreshList(int selectIndex);
    void LoadSelectedIntoControls();
    void StoreControlsIntoSelected();
    void OnSelectionChanged();
    void OnAdd();
    void OnRemove();
    void OnMove(int delta);
    void OnSave();
    void OnTrackbarChanged();

    // Follows the panel's palette: a dark title bar, dark child controls and a
    // dark background when the panel is dark, and the plain system look when it is
    // light or when Windows is in high contrast (where the user's own colours are
    // the point and must not be overridden). Safe to call again when the theme
    // changes under an open window.
    void ApplyTheme();
    LRESULT OnControlColor(UINT msg, HDC dc, HWND control);

    // The layout is written in logical (96-DPI) units and scaled here. The process
    // is per-monitor DPI aware, so nothing is stretched for us: fixed pixels came
    // out small and cramped from 125% up, and on a 4K TV beside a 100% monitor they
    // were worse. Rounding is done on the edges, not the sizes, so two controls
    // that touch in logical units touch in pixels too instead of overlapping by one.
    int Scale(int logical) const { return MulDiv(logical, static_cast<int>(m_dpi), 96); }
    HFONT MakeFont() const;
    // Re-lays out for a new DPI - the window was dragged to a monitor with another
    // scale - without recreating a single control.
    void ApplyDpi(UINT dpi);

    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_typeCombo = nullptr;
    HWND m_positionTrackbar = nullptr;
    HWND m_positionLabel = nullptr;
    HWND m_tabWidthEdit = nullptr;
    HWND m_tabHeightEdit = nullptr;
    HWND m_panelWidthEdit = nullptr;
    HWND m_addButton = nullptr;
    HWND m_removeButton = nullptr;
    HWND m_upButton = nullptr;
    HWND m_downButton = nullptr;
    HWND m_autostartCheck = nullptr;
    HWND m_themeCombo = nullptr;
    HWND m_fullscreenCheck = nullptr;
    HWND m_saveButton = nullptr;
    HWND m_closeButton = nullptr;
    HFONT m_font = nullptr;

    struct Placement {
        HWND hwnd;
        int x, y, w, h; // logical units
    };
    std::vector<Placement> m_placements;
    UINT m_dpi = 96;

    // Themed painting. Brushes exist only while the dark look is in use; with the
    // system look they stay null and every WM_CTLCOLOR* message falls through.
    bool m_themed = false;
    HBRUSH m_windowBrush = nullptr;  // dialog background
    HBRUSH m_controlBrush = nullptr; // edit boxes and lists
    COLORREF m_textColor = 0;
    COLORREF m_mutedColor = 0;
    COLORREF m_windowColor = 0;
    COLORREF m_controlColor = 0;

    std::vector<TabSettings> m_tabs;
    int m_selectedIndex = -1;
    SaveCallback m_onSave;
    ProblemCallback m_onProblem;

    // What the startup checkbox showed when the window opened. Saving only touches
    // the startup entry when the box was actually changed: writing it on every Save
    // rewrote the entry with whatever exe was running, so saving from a test copy
    // took autostart over, and re-wrote an entry the user had switched off.
    bool m_autostartInitial = false;
};

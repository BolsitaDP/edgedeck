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

    bool Create(HINSTANCE hInstance, std::vector<TabSettings> initial, SaveCallback onSave);
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
    HWND m_saveButton = nullptr;
    HWND m_closeButton = nullptr;
    HFONT m_font = nullptr;

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
};

#pragma once

#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "Config.h"
#include "DialogTheme.h"
#include "FileDrop.h"

// Modeless comctl32 settings dialog: lets the user pick each tab's widget
// type, monitor, edge, vertical position, and size, then reorder/add/remove tabs. Built
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
    // The "Actions..." button: the quick-actions list has a window of its own, which the owner
    // opens. Optional.
    using ActionsCallback = std::function<void()>;
    // The "Shortcuts..." button: the shortcuts have a window of their own, which the owner opens.
    using ShortcutsCallback = std::function<void()>;
    // The Displays tab's layouts have a window of their own too. Optional.
    using LayoutsCallback = std::function<void()>;

    bool Create(HINSTANCE hInstance, std::vector<TabSettings> initial, SaveCallback onSave,
                ProblemCallback onProblem = {}, ActionsCallback onEditActions = {},
                ShortcutsCallback onEditShortcuts = {}, LayoutsCallback onEditLayouts = {});
    HWND Hwnd() const { return m_hwnd; }

    // The reminder of the main shortcuts; called when they change while the window is open.
    void RefreshKeyboardHelp();

    // Tab index was moved with its panel's handle while this window is open. The list here is a
    // copy that may have been edited since, so the entry follows only if it still looks like that
    // tab (same type, same old position) - otherwise this window's own value is what its Save means.
    void TabMoved(size_t index, WidgetType type, float oldRatio, float newRatio);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls(HINSTANCE hInstance);
    // Which monitors a tab can be put on: the primary one, every monitor Windows knows of, and
    // any saved monitor that is not plugged in right now.
    void BuildMonitorChoices();
    // The open-delay values the combo offers (plus the configured one, if it is none of them).
    void BuildDelayChoices();
    int MonitorChoiceIndex(const std::wstring& id) const; // -1 when there is no such entry
    void RefreshList(int selectIndex);
    void LoadSelectedIntoControls();
    void StoreControlsIntoSelected();
    void OnSelectionChanged();
    void OnAdd();
    void OnRemove();
    void OnMove(int delta);
    void OnSave();
    void OnTrackbarChanged();
    void OnFoldersDropped(std::vector<std::wstring> paths);

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
    HFONT MakeFont(bool heading) const;
    // Re-lays out for a new DPI - the window was dragged to a monitor with another
    // scale - without recreating a single control.
    void ApplyDpi(UINT dpi);

    // A screen with little height (1366x768 at 125%, 1080p at 175%) has less room than the window
    // needs, and the footer - Save - would be off the bottom of it. The window is then made as
    // tall as the monitor's work area allows, a scroll bar's width wider, and its content scrolls.
    // FitRect trims a window rectangle to the work area of the monitor it is on; UpdateScroll sets
    // the scroll bar for the current size; ScrollTo moves the content.
    RECT FitRect(RECT window) const;
    void UpdateScroll();
    void ScrollTo(int y);
    int m_scrollY = 0; // pixels the content is scrolled up by

    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_typeCombo = nullptr;
    HWND m_positionTrackbar = nullptr;
    HWND m_positionLabel = nullptr;
    HWND m_tabWidthEdit = nullptr;
    HWND m_tabHeightEdit = nullptr;
    HWND m_panelWidthEdit = nullptr;
    HWND m_monitorCombo = nullptr;
    HWND m_edgeCombo = nullptr;
    HWND m_delayCombo = nullptr;
    HWND m_actionsButton = nullptr;
    HWND m_folderEdit = nullptr;
    HWND m_languageCombo = nullptr;
    HWND m_keyboardHelp = nullptr;
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
    HFONT m_headerFont = nullptr; // the section headings

    struct Placement {
        HWND hwnd;
        int x, y, w, h; // logical units
        bool heading;   // set in the heading font
    };
    std::vector<Placement> m_placements;
    UINT m_dpi = 96;

    // Themed painting (dark title bar, controls and background when the panels are dark).
    DialogTheme m_theme;

    struct MonitorChoice {
        std::wstring id; // empty = the primary monitor
        std::wstring label;
    };
    std::vector<MonitorChoice> m_monitorChoices;

    std::vector<TabSettings> m_tabs;
    int m_selectedIndex = -1;
    SaveCallback m_onSave;
    ProblemCallback m_onProblem;
    ActionsCallback m_onEditActions;
    ShortcutsCallback m_onEditShortcuts;
    LayoutsCallback m_onEditLayouts;
    std::vector<int> m_delayChoices; // milliseconds, parallel to the combo box

    // The folder the Recent files tabs list (a setting of the app, not of one tab): held here while
    // the window is open, shown in the folder box when the selected tab is a Recent files one, and
    // handed to Config on Save. A folder dragged from Explorer onto the window sets it.
    std::wstring m_recentFolder;
    FileDrop::Zone m_drop;

    // What the startup checkbox showed when the window opened. Saving only touches
    // the startup entry when the box was actually changed: writing it on every Save
    // rewrote the entry with whatever exe was running, so saving from a test copy
    // took autostart over, and re-wrote an entry the user had switched off.
    bool m_autostartInitial = false;
};

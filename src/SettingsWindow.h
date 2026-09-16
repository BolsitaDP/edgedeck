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
    HWND m_saveButton = nullptr;
    HWND m_closeButton = nullptr;
    HFONT m_font = nullptr;

    std::vector<TabSettings> m_tabs;
    int m_selectedIndex = -1;
    SaveCallback m_onSave;
};

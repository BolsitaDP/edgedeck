#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "DialogTheme.h"
#include "DisplayLayouts.h"
#include "DisplayTopology.h"

// Where the Displays tab's layouts are edited: a list of them (their order is their shortcuts'
// order), a name, and a box for each monitor it turns on. "Add current" makes one from what is on
// right now, which is the quick way to save the arrangement in front of you. Built and scaled like
// the actions editor.
class LayoutsWindow {
public:
    // Called with the edited list when Save is pressed; the window closes after it. An empty list
    // is valid: it means the automatic layouts.
    using SaveCallback = std::function<void(std::vector<DisplayLayouts::Layout>)>;

    bool Create(HINSTANCE hInstance, std::vector<DisplayLayouts::Layout> initial, SaveCallback onSave);
    HWND Hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls(HINSTANCE hInstance);
    void RefreshList(int selectIndex);
    void LoadSelectedIntoControls();
    void StoreControlsIntoSelected();
    void OnNameEdited();
    void OnAddCurrent();
    void OnRemove();
    void OnMove(int delta);
    void OnSave();
    void SetStatus(const wchar_t* text);

    int Scale(int logical) const { return MulDiv(logical, static_cast<int>(m_dpi), 96); }
    HFONT MakeFont(bool heading) const;
    void ApplyDpi(UINT dpi);

    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_nameEdit = nullptr;
    HWND m_addButton = nullptr;
    HWND m_removeButton = nullptr;
    HWND m_upButton = nullptr;
    HWND m_downButton = nullptr;
    HWND m_status = nullptr;
    std::vector<HWND> m_monitorChecks; // one per entry of m_monitors
    HFONT m_font = nullptr;
    HFONT m_headingFont = nullptr;

    struct Placement {
        HWND hwnd;
        int x, y, w, h; // logical units
        bool heading;   // set in the heading font
    };
    std::vector<Placement> m_placements;
    UINT m_dpi = 96;
    DialogTheme m_theme;

    // The monitors a layout can name: every connected one, and any a saved layout names that is not
    // plugged in now (so editing and saving does not quietly drop it).
    struct MonitorChoice {
        std::wstring id;
        std::wstring label;
    };
    std::vector<MonitorChoice> m_monitors;
    std::vector<std::wstring> m_activeIds; // what is on right now, for "Add current"
    std::vector<DisplayTopology::Monitor> m_connected;

    std::vector<DisplayLayouts::Layout> m_layouts;
    int m_selected = -1;
    bool m_loading = false;
    SaveCallback m_onSave;
};

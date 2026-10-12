#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "DialogTheme.h"
#include "QuickAction.h"

// Where the Quick Actions list is edited: the rows, what each is called and what it opens. A
// window of its own (modeless, like Settings) because the list is shared by every Quick Actions
// tab and not part of any one tab's settings. Built from comctl32 controls laid out in logical
// 96-DPI units and scaled, so it follows the monitor it opens on and re-lays out when dragged to
// one with another scale - the same way Settings does.
class ActionsWindow {
public:
    // Called with the edited list when Save is pressed; the window closes after it.
    using SaveCallback = std::function<void(std::vector<QuickAction>)>;

    bool Create(HINSTANCE hInstance, std::vector<QuickAction> initial, SaveCallback onSave);
    HWND Hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls(HINSTANCE hInstance);
    void RefreshList(int selectIndex);
    void LoadSelectedIntoControls();
    void StoreControlsIntoSelected();
    void OnAdd();
    void OnRemove();
    void OnMove(int delta);
    void OnBrowse();
    void OnNameEdited();
    void OnSave();
    void SetStatus(const wchar_t* text);

    int Scale(int logical) const { return MulDiv(logical, static_cast<int>(m_dpi), 96); }
    HFONT MakeFont() const;
    void ApplyDpi(UINT dpi);

    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_nameEdit = nullptr;
    HWND m_targetEdit = nullptr;
    HWND m_argsEdit = nullptr;
    HWND m_browseButton = nullptr;
    HWND m_addButton = nullptr;
    HWND m_removeButton = nullptr;
    HWND m_upButton = nullptr;
    HWND m_downButton = nullptr;
    HWND m_status = nullptr;
    HFONT m_font = nullptr;

    struct Placement {
        HWND hwnd;
        int x, y, w, h; // logical units
    };
    std::vector<Placement> m_placements;
    UINT m_dpi = 96;

    DialogTheme m_theme;

    std::vector<QuickAction> m_actions;
    int m_selected = -1;
    bool m_loading = false; // the edit boxes are being filled, not typed in
    SaveCallback m_onSave;
};

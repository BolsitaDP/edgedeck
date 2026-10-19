#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "DialogTheme.h"
#include "Shortcuts.h"

// Where the system-wide shortcuts are chosen: exit and pause as whole chords (Windows' own hotkey
// control), opening a tab and switching displays as a choice of modifiers for their digits. A
// window of its own, like the actions editor, opened from Settings. Laid out in logical 96-DPI units
// and scaled, re-laid out when dragged to a monitor with another scale.
class ShortcutsWindow {
public:
    // Called with a set that passed Shortcuts::Problem. Returns what could not be registered, as a
    // sentence for the window to show (it then stays open), or empty when all of it was.
    using SaveCallback = std::function<std::wstring(const Shortcuts::Set&)>;

    bool Create(HINSTANCE hInstance, Shortcuts::Set initial, SaveCallback onSave);
    HWND Hwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls(HINSTANCE hInstance);
    void LoadIntoControls();
    Shortcuts::Set ReadControls() const;
    void OnSave();
    void SetStatus(const wchar_t* text);

    int Scale(int logical) const { return MulDiv(logical, static_cast<int>(m_dpi), 96); }
    HFONT MakeFont() const;
    void ApplyDpi(UINT dpi);

    HWND m_hwnd = nullptr;
    HWND m_exitHotkey = nullptr;
    HWND m_pauseHotkey = nullptr;
    HWND m_tabsCombo = nullptr;
    HWND m_displaysCombo = nullptr;
    HWND m_status = nullptr;
    HFONT m_font = nullptr;

    struct Placement {
        HWND hwnd;
        int x, y, w, h; // logical units
    };
    std::vector<Placement> m_placements;
    UINT m_dpi = 96;
    DialogTheme m_theme;

    Shortcuts::Set m_initial;
    // Windows' hotkey control cannot show the Win key, so a chord with Win (set in config.txt) is
    // kept as it is unless its box is actually edited.
    bool m_exitEdited = false;
    bool m_pauseEdited = false;
    SaveCallback m_onSave;
};

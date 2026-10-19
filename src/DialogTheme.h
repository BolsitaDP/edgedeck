#pragma once

#include <windows.h>

// The look of EdgeDeck's own comctl32 windows (Settings, the quick-actions editor): the same
// palette the panels use, so an "Always dark" override darkens them too and a light override on a
// dark system keeps them light.
//
// Windows has no dark mode for common controls as such. What it has is a set of alternate visual
// styles that Explorer and the common dialogs use, picked per control with SetWindowTheme, a
// title-bar attribute for the caption, and WM_CTLCOLOR* for the rest. With the system look - a
// light panel, or high contrast, where the user's own colours are the point - nothing is
// overridden and every message falls through to the default handling.
class DialogTheme {
public:
    DialogTheme() = default;
    ~DialogTheme() { Release(); }
    DialogTheme(const DialogTheme&) = delete;
    DialogTheme& operator=(const DialogTheme&) = delete;

    // Reads the current palette and applies it to the window, its title bar and its children.
    // Safe to call again when the theme changes under an open window.
    void Apply(HWND window);

    // True while the dark look is in use. The two handlers below only act then.
    bool Themed() const { return m_themed && m_windowBrush && m_controlBrush; }

    // WM_ERASEBKGND: fills the client area. Returns true if it painted.
    bool EraseBackground(HWND window, HDC dc) const;

    // WM_CTLCOLORSTATIC/EDIT/LISTBOX/BTN: the brush to return. An edit box reports itself as a
    // STATIC while it is disabled or read-only, so the message alone cannot tell a field from a
    // label; the caller says so with alsoField (by control id).
    LRESULT ControlColor(UINT message, HDC dc, HWND control, bool alsoField) const;

    // For a label that is secondary (a version line, a summary) and for the thin lines between
    // sections, in either look: the panel's palette while themed, the system's colours otherwise -
    // which in high contrast are the user's own. SecondaryLabel and Divider answer the label's
    // and the line's WM_CTLCOLORSTATIC.
    LRESULT SecondaryLabel(HDC dc) const;
    LRESULT Divider(HDC dc) const;

    // At WM_DESTROY.
    void Release();

private:
    bool m_themed = false;
    HBRUSH m_windowBrush = nullptr;  // dialog background
    HBRUSH m_controlBrush = nullptr; // edit boxes and lists
    HBRUSH m_dividerBrush = nullptr; // lines between sections, in either look
    COLORREF m_dividerColor = 0;
    COLORREF m_textColor = 0;
    COLORREF m_mutedColor = 0;
    COLORREF m_windowColor = 0;
    COLORREF m_controlColor = 0;
};

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

    // The dark look's colours, for a control that paints itself (the hotkey box, which no visual
    // style reaches). Only meaningful while Themed().
    COLORREF FieldColor() const { return m_controlColor; }
    COLORREF TextColor() const { return m_textColor; }
    COLORREF AccentColor() const { return m_accentColor; }
    // A field's outline: between the field and the text, about where the dark combo boxes draw theirs.
    COLORREF OutlineColor() const {
        auto mix = [](BYTE field, BYTE text) { return static_cast<BYTE>((field * 7 + text * 3) / 10); };
        return RGB(mix(GetRValue(m_controlColor), GetRValue(m_textColor)),
                   mix(GetGValue(m_controlColor), GetGValue(m_textColor)),
                   mix(GetBValue(m_controlColor), GetBValue(m_textColor)));
    }

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

    // Control ids every dialog uses for those two kinds of static: an empty one in the divider
    // range is a line, one in the secondary range a label in the secondary tone.
    static constexpr int kDividerIdFirst = 9900;
    static constexpr int kDividerIdLast = 9909;
    static constexpr int kSecondaryIdFirst = 9910;
    static constexpr int kSecondaryIdLast = 9919;
    // WM_CTLCOLORSTATIC for a static in either range; 0 for any other, which the caller colours as
    // before.
    LRESULT SpecialStatic(HDC dc, HWND control) const;

    // The dialogs' font at a DPI: Windows 11's interface face (Segoe UI Variable) where it is
    // installed, Segoe UI before it; 14 logical pixels, or 16 and semibold for a section heading.
    static HFONT MakeFont(UINT dpi, bool heading);

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
    COLORREF m_accentColor = 0;
};

#pragma once

#include <windows.h>

#include <string>

// The keyboard shortcuts EdgeDeck registers system-wide, as a setting. Another program may already
// own one of the defaults - Ctrl+Alt+Q is popular - and the only fix used to be a rebuild.
//
// Two kinds: a single chord (exit, pause), and a family that is modifiers plus a digit (open tab
// 1..9, switch displays 1..4). Any of them can be off. Text forms are what config.txt holds:
// "Ctrl+Alt+Q", "Ctrl+Alt+Shift" for a family, "none" for off. Kept free of windows so the rules
// - what is a valid shortcut, which ones clash - are tested on their own.
struct Shortcut {
    UINT modifiers = 0; // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT key = 0;       // a virtual-key code: 'A'..'Z', '0'..'9', VK_F1..VK_F24; 0 = off

    bool Off() const { return key == 0; }
    bool operator==(const Shortcut& other) const { return modifiers == other.modifiers && key == other.key; }
};

namespace Shortcuts {

inline constexpr UINT kModifierMask = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
inline constexpr int kTabCount = 9;     // open tab 1..9
inline constexpr int kDisplayCount = 9; // the Displays tab's layouts 1..9 (DisplayLayouts::kMaxLayouts)

struct Set {
    Shortcut exit{MOD_CONTROL | MOD_ALT, 'Q'};
    Shortcut pause{MOD_CONTROL | MOD_ALT, 'P'};
    UINT tabModifiers = MOD_CONTROL | MOD_ALT;                  // + 1..9; 0 = off
    UINT displayModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;  // + 1..9; 0 = off

    bool operator==(const Set& o) const {
        return exit == o.exit && pause == o.pause && tabModifiers == o.tabModifiers &&
               displayModifiers == o.displayModifiers;
    }
};

// Reading and writing config.txt. A value that does not parse leaves `out` untouched, so a typo
// keeps the default rather than silently switching a shortcut off; "none" is off.
bool Parse(const std::wstring& text, Shortcut& out);
bool ParseModifiers(const std::wstring& text, UINT& out);
std::wstring Format(const Shortcut& shortcut);       // "Ctrl+Alt+Q", or "none"
std::wstring FormatModifiers(UINT modifiers);         // "Ctrl+Alt", or "none"

// The same, for showing a person: key names in the current language ("Mayús" for Shift in
// Spanish). A family is shown with its digits: "Ctrl+Alt+1..9".
std::wstring Display(const Shortcut& shortcut);
std::wstring DisplayFamily(UINT modifiers, int count);
// The modifiers alone, as a shortcut being typed shows them: "Ctrl+Alt+". Empty for none.
std::wstring DisplayModifiers(UINT modifiers);

// A shortcut needs Ctrl, Alt or Win: one that is Shift+letter, or a bare key, would take that key
// away from everything the user types, everywhere.
bool HasRequiredModifier(UINT modifiers);

// Why this set cannot be used, as a sentence for the user, or empty when it can: a shortcut
// without Ctrl/Alt/Win, or two that are the same keys (exit and pause alike; a chord that is one
// of a family's digits; two families with the same modifiers).
std::wstring Problem(const Set& set);

} // namespace Shortcuts

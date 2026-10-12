#pragma once

#include <string>
#include <vector>

// One row of the Quick Actions panel: what it says and what it starts. Held as plain data so the
// config file, the editor window and the widget share one definition, and so the rules about
// what a valid list is can be tested without a window.
struct QuickAction {
    std::wstring name;      // the row's text
    // What is opened, the way Run would open it: a program, a document, a folder or a web address.
    // %Variables% are expanded. kShowDesktop is the one thing that is not a thing to open.
    std::wstring target;
    std::wstring arguments; // optional; only meaningful for a program
};

namespace QuickActions {

// "Show the desktop" is not a file or an address but a request to the shell.
inline constexpr wchar_t kShowDesktop[] = L"builtin:desktop";

// Bounds that keep a panel, and a hand-edited file, sane. The row count is what the panel's
// height is made of; the lengths are what fits a row and what a path can be.
inline constexpr size_t kMaxActions = 12;
inline constexpr size_t kMaxNameLength = 40;
inline constexpr size_t kMaxTargetLength = 520;
inline constexpr size_t kMaxArgumentsLength = 260;

// What a fresh install shows: Notepad, Calculator, Show Desktop.
const std::vector<QuickAction>& Defaults();

// A list as it will be kept: whitespace trimmed, line breaks removed (the config file is one
// value per line), lengths and count capped, entries with no name or nothing to open dropped.
std::vector<QuickAction> Sanitize(std::vector<QuickAction> list);

} // namespace QuickActions

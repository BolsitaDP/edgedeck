#pragma once

#include <windows.h>

#include <string>

#include "QuickAction.h"

// What the Quick Actions panel can do to the system. Everything here reports failure as a
// sentence for the user and never raises a dialog of its own: the panel shows the sentence in its
// header, so a bad row is a note and not a window that takes focus.
namespace Actions {

// Starts one quick action: opens its target the way Run would (a program, a document, a folder, a
// web address), with its arguments, or asks the shell to show the desktop. Returns false and sets
// `problem` when Windows could not.
bool Run(const QuickAction& action, std::wstring& problem);

// True when `path` is a file whose type has no program registered: what Windows would answer with
// its "How do you want to open this file?" window, which takes the focus - exactly what a panel
// must not do. The shell's own answer for such a type is OpenWith.exe, so that is what is looked
// for; a type whose lookup merely fails (Store apps' types, shell-handled ones) does open. Folders,
// addresses and anything that is not a file on disk are never "nothing opens it".
bool NothingOpens(const std::wstring& path);

// Opens a path exactly as given - no %Variable% expansion, which belongs to what a user typed into
// a row and would rewrite a real file name containing percent signs. `what` names it in the
// sentence that says why it failed.
bool Open(const std::wstring& path, const std::wstring& what, std::wstring& problem);

// "%ProgramFiles%\Tool\tool.exe" -> the real path. Unknown variables are left as written, which
// is what ExpandEnvironmentStrings does and what makes the failure readable.
std::wstring ExpandVariables(const std::wstring& text);

// A short phrase for why ShellExecute failed, from its Win32 error: "not found", "access denied",
// "nothing is set to open that kind of file". Exposed for the tests.
std::wstring DescribeShellError(DWORD error);

// Asks the shell to toggle the desktop (Win+D), falling back to sending the keys. What the
// "builtin:desktop" target runs.
bool ShowDesktopToggle();

} // namespace Actions

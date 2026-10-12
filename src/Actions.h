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

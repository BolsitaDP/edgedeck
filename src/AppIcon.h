#pragma once

#include <windows.h>

namespace AppIcon {

// EdgeDeck's icon from the executable's resources, at the size Windows uses for small icons (the
// notification area, title bars) or large ones, for the current DPI. Never null: where the
// module has no such icon (a test tool that hosts the App without the resource script) it is the
// generic application icon.
//
// The caller owns it and destroys it with DestroyIcon, unless it is handed to something that
// takes it over (a window class, which keeps it for the life of the process).
HICON Load(HINSTANCE instance, bool wantSmall);

} // namespace AppIcon

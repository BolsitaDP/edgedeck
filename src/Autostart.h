#pragma once

// Start-with-Windows via the per-user Run key (HKCU, no admin needed).
// EdgeDeck is a GUI-subsystem exe, so a login launch opens no console window.
namespace Autostart {

bool IsEnabled();
bool SetEnabled(bool enabled);

} // namespace Autostart

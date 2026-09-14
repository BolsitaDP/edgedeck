#pragma once

#include <windows.h>

namespace MediaControls {

enum class SpotifyCommand {
    Previous,
    PlayPause,
    Next,
};

enum class Result {
    Success,
    SpotifyNotFound,
    Unsupported,
    Failed,
};

// Uses the Windows media session exposed by Spotify. Work runs only after a
// button press; completion is posted back to the UI window.
void SendSpotifyCommand(SpotifyCommand command, HWND notifyWindow, UINT notifyMessage);

} // namespace MediaControls

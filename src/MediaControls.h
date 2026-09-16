#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

namespace MediaControls {

enum class Command {
    Previous,
    PlayPause,
    Next,
};

enum class Result {
    Success,
    Unsupported,
    Failed,
};

struct SessionInfo {
    std::wstring displayName;
    bool canPrevious = false;
    bool canPlayPause = false;
    bool canNext = false;
    bool isPlaying = false;
};

// Opaque handle to one active session, valid until the caller's next
// RefreshSessions replaces it. Keeps the actual WinRT session type out of
// every header that doesn't need it (Tab, App, etc.) - only
// MediaControls.cpp knows what's really behind this.
class SessionHandle {
public:
    virtual ~SessionHandle() = default;
};

struct SessionList {
    std::vector<SessionInfo> infos;
    std::vector<std::shared_ptr<SessionHandle>> handles; // same size/order as infos
};

// Enumerates every app currently exposing a media session to Windows -
// Spotify, a browser tab, VLC, etc, whichever ones are actually active, not
// just one "current" pick. Posts a `SessionList*` back via
// notifyMessage/wParam; the receiver takes ownership and must delete it.
void RefreshSessions(HWND notifyWindow, UINT notifyMessage);

// Sends a transport command to one specific session from a previous
// RefreshSessions result. `tag` is opaque to MediaControls (callers pack a
// row index or similar into it) and comes back combined with the result via
// notifyMessage/wParam - see UnpackResult. Needed because commands for
// different rows can be in flight at once and may finish out of order.
void SendCommand(std::shared_ptr<SessionHandle> session, Command command, int tag,
                  HWND notifyWindow, UINT notifyMessage);

void UnpackResult(WPARAM wParam, int& outTag, Result& outResult);

} // namespace MediaControls

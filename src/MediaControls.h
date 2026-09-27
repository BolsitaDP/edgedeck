#pragma once

#include <windows.h>

#include "AsyncResult.h"

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
    // Windows' own identifier for the session's source app (an AUMID, or a
    // plain exe path for classic desktop apps). Used to tell whether a row
    // from an older list is still the same row, so a late command reply can
    // never be applied to a different app after a refresh swapped the list.
    std::wstring sourceId;
    std::wstring displayName;
    bool canPrevious = false;
    bool canPlayPause = false;
    bool canNext = false;
    bool isPlaying = false;
    std::wstring title;
    std::wstring artist;
    long long positionMs = 0; // -1 when the app does not report a position
    long long durationMs = 0; // 0 for a live stream or unknown length
};

// Opaque handle to one active session, valid until the caller's next
// RefreshSessions replaces it. Keeps the actual WinRT session type out of
// every header that doesn't need it (Tab, App, etc.) - only
// MediaControls.cpp knows what's really behind this.
class SessionHandle {
public:
    virtual ~SessionHandle() = default;
};

// Reply to RefreshSessions. `listGeneration` is supplied by the caller and
// echoed back untouched, so the widget can tell "this is the list I am
// currently showing" from "this is a list that has already been replaced".
struct SessionList : AsyncEnvelope {
    std::uint64_t listGeneration = 0;
    std::vector<SessionInfo> infos;
    std::vector<std::shared_ptr<SessionHandle>> handles; // same size/order as infos
};

// Reply to SendCommand. Carries the caller's `tag` plus the generation of the
// list the command was issued against, for the same reason.
struct CommandResult : AsyncEnvelope {
    int tag = 0;
    std::uint64_t listGeneration = 0;
};

// Enumerates every app currently exposing a media session to Windows -
// Spotify, a browser tab, VLC, etc, whichever ones are actually active, not
// just one "current" pick.
//
// Posts a SessionList* to notifyWindow as notifyMessage, taking ownership
// either way (see PostOrDelete). The caller stamps the envelope with
// `requestId` (its own in-flight id) and `listGeneration`.
void RefreshSessions(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
                     std::uint64_t listGeneration);

// Sends a transport command to one specific session from a previous
// RefreshSessions result. Posts a CommandResult* carrying `tag` and
// `listGeneration` back to notifyWindow. Needed because commands for different
// rows can be in flight at once and may finish out of order.
void SendCommand(std::shared_ptr<SessionHandle> session, Command command, int tag,
                 std::uint64_t requestId, std::uint64_t listGeneration, HWND notifyWindow,
                 UINT notifyMessage);

// A cheap "what is playing right now" probe, used by widgets that only need
// the current position (Lyrics) rather than the whole session list. WinRT
// answers this in-process from already-published state, so it costs nothing
// like a full enumeration.
struct NowPlaying : AsyncEnvelope {
    bool hasSession = false;
    bool isPlaying = false;
    std::wstring sourceId;
    std::wstring title;
    std::wstring artist;
    long long positionMs = 0;
    long long durationMs = 0;
};

void QueryNowPlaying(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId);

// Long-lived change notification. The returned object keeps a
// GlobalSystemMediaTransportControlsSessionManager alive and subscribes to its
// events, so a widget learns about a new session or a playback-state change the
// moment it happens instead of only when its panel is reopened. The
// subscription ends when the returned shared_ptr is destroyed.
//
// Posting is fire-and-forget: the handler only ever enqueues a
// "something changed" notice, so it is safe on whatever thread the event
// arrives on, and a torn-down tab simply makes the post fail.
class MediaChangeSubscription {
public:
    virtual ~MediaChangeSubscription() = default;
};

std::shared_ptr<MediaChangeSubscription> SubscribeToChanges(HWND notifyWindow, UINT notifyMessage);

} // namespace MediaControls


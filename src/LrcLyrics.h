#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Synced lyrics for whatever is currently playing - any app exposing a media
// session to Windows (Spotify, a browser tab, VLC, etc), same "whichever one
// Windows thinks is current" pick the rest of the app uses. Fetched from
// LRCLIB (lrclib.net): a free, public, keyless lyrics database built for
// exactly this purpose - no account, no cookie, no OAuth, not tied to
// Spotify at all.
namespace LrcLyrics {

struct Line {
    int timeMs = 0;
    std::wstring text;
};

enum class Status {
    Success,
    NothingPlaying, // no active media session, or it reports no title
    NoLyrics,       // LRCLIB has nothing synced for this track
    NetworkError,
};

struct Result {
    Status status = Status::NetworkError;
    std::wstring trackKey; // "Artist - Title", used for caching and display
    std::vector<Line> lines;
    int positionMs = 0; // local playback position at fetch time
};

// Checks a local cache (keyed by title/artist - zero network cost on a hit)
// before querying LRCLIB. Runs entirely on a background thread-pool job
// kicked off by the panel opening; posts a `Result*` back via
// notifyMessage/wParam. The receiver owns it and must delete it.
void FetchCurrentLyrics(HWND notifyWindow, UINT notifyMessage);

} // namespace LrcLyrics

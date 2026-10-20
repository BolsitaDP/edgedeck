#pragma once

#include <windows.h>

#include "AsyncResult.h"

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
    NoLyrics,       // LRCLIB has no lyrics for this track, synced or plain
    NetworkError,
};

struct Result : AsyncEnvelope {
    AsyncKind Kind() const override { return AsyncKind::Lyrics; }
    Status status = Status::NetworkError;
    std::wstring trackKey; // "Artist - Title", used for caching and display
    std::vector<Line> lines;
    // Synced lyrics have a time on every line. Plain ones - all LRCLIB has for many songs - have
    // none (timeMs -1): the panel shows them as text that scrolls with the song.
    bool synced = true;
    int positionMs = 0; // local playback position at fetch time
    int durationMs = 0; // the track's length, 0 when the player does not say
    // How much later than their times the lines are shown: the user's correction for lyrics timed
    // to another release of the song, remembered with the track's cached lyrics.
    int offsetMs = 0;
};

// The correction the user can make, in steps of kOffsetStepMs up to kMaxOffsetMs either way.
inline constexpr int kOffsetStepMs = 500;
inline constexpr int kMaxOffsetMs = 30000;

// One record LRCLIB offered for a search. Its fields may be missing or JSON null; empty here.
struct Candidate {
    int durationMs = 0; // 0 when not known
    bool instrumental = false;
    std::wstring synced; // LRC text
    std::wstring plain;  // plain text
};

// Which record to show for a track durationMs long (0: unknown), and whether it is the synced one.
// Synced lyrics are shown only for a recording of the same length (within kDurationToleranceMs):
// another recording's timings would highlight the wrong lines. Failing that, plain lyrics -
// the closest in length - and only when there are none, synced lyrics of another length.
// Instrumental records are skipped. Index -1: nothing to show.
inline constexpr int kDurationToleranceMs = 10000;
struct Choice {
    int index = -1;
    bool synced = false;
};
Choice PickLyrics(const std::vector<Candidate>& candidates, int durationMs);

// Plain lyrics as lines without times (timeMs -1): one per line of text, a run of blank lines kept
// as one blank line (verses stay apart), none at either end.
std::vector<Line> PlainLines(const std::wstring& text);

// The lyrics cache file's text and back. "#plain" on the first line marks plain lyrics; a file
// without it (every file written before) is synced lines, "time<TAB>text". "#offset<TAB>ms" keeps
// the user's correction (none: 0).
std::wstring SerializeCache(const std::vector<Line>& lines, bool synced, int offsetMs = 0);
bool ParseCache(const std::wstring& text, std::vector<Line>& lines, bool& synced, int* offsetMs = nullptr);

// Remembers the user's correction for a track whose lyrics are cached, on a pool thread (it
// rewrites the cache file). Nothing to do for a track with no cached lyrics.
void SaveOffset(const std::wstring& trackKey, int offsetMs);

// Parses LRC text ("[mm:ss.xx]line", repeatable and multi-timestamp tags) into
// sorted, timestamped lines. Exposed for tests; not used outside this module at
// runtime.
std::vector<Line> ParseLrc(const std::wstring& lrc);

// Checks a local cache (keyed by title/artist - zero network cost on a hit)
// before querying LRCLIB, and records negative hits too, so a track that has no
// synced lyrics is not re-requested on every single panel open. Runs entirely
// on a background thread-pool job kicked off by the panel opening; posts a
// Result* back to notifyWindow as notifyMessage, taking ownership either way
// (see PostOrDelete). The caller stamps the envelope with its in-flight
// `requestId`.
void FetchCurrentLyrics(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId);

} // namespace LrcLyrics

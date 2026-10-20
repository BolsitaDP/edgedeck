#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <chrono>

namespace MediaPosition {

// Where a playing track is now. Windows keeps the position an app last reported and when it
// reported it, and apps report seldom - Spotify every four or five seconds - so the position as
// read is up to that far behind: lyrics lit each line late and then jumped. While playing, the
// time since the report is added (up to the track's end). A report claimed to be from the future,
// or from hours ago (an app that never set the time), is taken as it is.
inline long long NowMs(long long reportedMs, long long reportedAgoMs, long long durationMs, bool playing) {
    constexpr long long kStaleMs = 6LL * 60 * 60 * 1000;
    if (!playing || reportedMs < 0 || reportedAgoMs <= 0 || reportedAgoMs > kStaleMs) return reportedMs;
    const long long now = reportedMs + reportedAgoMs;
    return durationMs > 0 && now > durationMs ? durationMs : now;
}

// The same, read from a session.
inline long long NowMs(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession const& session) {
    using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    const auto timeline = session.GetTimelineProperties();
    const auto reported = timeline.LastUpdatedTime();
    long long agoMs = 0;
    if (reported.time_since_epoch().count() != 0) {
        agoMs = std::chrono::duration_cast<std::chrono::milliseconds>(winrt::clock::now() - reported).count();
    }
    const bool playing =
        session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    return NowMs(timeline.Position().count() / 10000, agoMs, timeline.EndTime().count() / 10000, playing);
}

} // namespace MediaPosition

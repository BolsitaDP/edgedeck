#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <mutex>

// The one media session manager every query shares.
//
// Windows keeps about 4 KB for every manager it hands out (RequestAsync) and never gives it back:
// measured at 56 heap blocks, 4.4 KB, per request, against nothing at all for queries made through
// one kept manager. EdgeDeck asked for a new one on every Media refresh (two per panel open), every
// lyrics lookup, and every lyrics position sample - twice a second while that panel is open - so
// its memory grew with use. Now it asks once.
//
// Inline, with its state in one function, so the test binary - which compiles LrcLyrics.cpp but not
// MediaControls.cpp - links without anything else.
namespace MediaManager {

using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;

namespace detail {
struct State {
    std::mutex mutex;
    Manager manager{nullptr};
};
// Deliberately never destroyed: a static destructor would release the manager at exit, after the
// apartments it came from are gone - releasing COM objects then is how crashes inside system DLLs
// happen. The process ending takes it with it.
inline State& Shared() {
    static State* state = new State();
    return *state;
}
} // namespace detail

// The shared manager, asked for the first time it is needed. Must be awaited from a pool thread,
// like everything else that touches media sessions here.
inline winrt::Windows::Foundation::IAsyncOperation<Manager> GetAsync() {
    detail::State& state = detail::Shared();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.manager) co_return state.manager;
    }
    Manager fresh = co_await Manager::RequestAsync();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.manager) state.manager = fresh; // two first callers at once keep the first
    co_return state.manager;
}

// After a call through the shared manager failed: the media service may have restarted, which
// leaves the old manager dead. The next GetAsync asks for a new one.
inline void Forget() {
    detail::State& state = detail::Shared();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.manager = nullptr;
}

} // namespace MediaManager

#include "Diagnostics.h"
#include "MediaControls.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <atomic>
#include <mutex>

namespace MediaControls {
namespace {

using WinRTSession = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using Manager = winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

// The four delegate types the change subscription registers. They are named
// because each registration is stored, and a stored registration has to own its
// handler - see the comment on ChangeSubscription::Watched.
using CurrentSessionHandler = winrt::Windows::Foundation::TypedEventHandler<
    Manager, winrt::Windows::Media::Control::CurrentSessionChangedEventArgs>;
using SessionsChangedHandler = winrt::Windows::Foundation::TypedEventHandler<
    Manager, winrt::Windows::Media::Control::SessionsChangedEventArgs>;
using PlaybackHandler =
    winrt::Windows::Foundation::TypedEventHandler<WinRTSession,
                                                  winrt::Windows::Media::Control::
                                                      PlaybackInfoChangedEventArgs>;
using MediaPropsHandler =
    winrt::Windows::Foundation::TypedEventHandler<WinRTSession,
                                                  winrt::Windows::Media::Control::
                                                      MediaPropertiesChangedEventArgs>;

class ConcreteSessionHandle : public SessionHandle {
public:
    explicit ConcreteSessionHandle(WinRTSession s) : session(std::move(s)) {}
    WinRTSession session{nullptr};
};

// AUMIDs are sometimes a plain exe path ("C:\...\Spotify.exe"), sometimes a
// package family id. Trim what's easy so classic desktop apps show a clean
// name; anything else is shown close to as-is rather than guessed at.
std::wstring PrettyName(winrt::hstring const& aumid) {
    std::wstring name{aumid.c_str(), aumid.size()};
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, L".exe") == 0) {
        name = name.substr(0, name.size() - 4);
    }
    return name.empty() ? L"Media" : name;
}

SessionInfo DescribeSession(WinRTSession const& session) {
    SessionInfo info;
    winrt::hstring aumid = session.SourceAppUserModelId();
    info.sourceId.assign(aumid.c_str(), aumid.size());
    info.displayName = PrettyName(aumid);

    auto controls = session.GetPlaybackInfo().Controls();
    info.canPrevious = controls.IsPreviousEnabled();
    info.canPlayPause = controls.IsPlayPauseToggleEnabled();
    info.canNext = controls.IsNextEnabled();
    info.isPlaying = session.GetPlaybackInfo().PlaybackStatus() ==
                     GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

    auto timeline = session.GetTimelineProperties();
    info.positionMs = timeline.Position().count() / 10000;
    info.durationMs = timeline.EndTime().count() / 10000;
    return info;
}

// ---------------------------------------------------------------------------
// Live change subscription
// ---------------------------------------------------------------------------

// Owns the one process-wide manager and the event registrations on it.
//
// Everything that touches `manager` happens on a thread-pool (MTA) thread, and
// nothing ever touches it from the UI thread - that is what keeps this free of
// any apartment marshalling. The event handlers are pure PostOrDelete calls, so
// they are safe on whatever thread WinRT picks for the callback. Unregistering
// from ~ChangeSubscription (which runs on the UI thread) hops to the pool as
// well, so it obeys the same rule.
class ChangeSubscription final : public MediaChangeSubscription {
public:
    ChangeSubscription(HWND notifyWindow, UINT notifyMessage) {
        auto state = std::make_shared<State>();
        state->notifyWindow = notifyWindow;
        state->notifyMessage = notifyMessage;
        m_state = state;
        RegisterAsync(state);
    }

    ~ChangeSubscription() override {
        if (!m_state) return;
        // Stop the handlers from doing anything, then tear the registrations
        // down on a pool thread. The coroutine owns `state` until it finishes,
        // so it cannot be destroyed underneath it.
        m_state->alive = false;
        UnregisterAsync(m_state);
        m_state.reset();
    }

private:
    // A registration is three objects that have to live and die together: the
    // source, the token, and - the part that is easy to miss - the handler.
    //
    // C++/WinRT's add_* stores only a *weak* reference to the delegate. Passing
    // a temporary lambda straight into add_ therefore leaves a live token
    // pointing at a handler that died at the end of the statement, and the next
    // time Windows raises the event it dereferences that corpse: an access
    // violation a couple of dozen bytes into a null vtable, on a thread-pool
    // thread, with no usable stack to walk. It showed up as a crash that had
    // nothing to do with anything the user was doing.
    //
    // Holding the session here also guarantees the token stays valid: the
    // session outlives the subscription anyway (MediaWidget keeps a handle per
    // row), so tearing the registration down has to go through the session.
    struct Watched {
        WinRTSession session;
        winrt::event_token playbackToken{};
        winrt::event_token propertiesToken{};
        PlaybackHandler playbackHandler{};
        MediaPropsHandler propertiesHandler{};
    };

    struct State {
        std::atomic<bool> alive{true};
        HWND notifyWindow = nullptr;
        UINT notifyMessage = 0;

        // Guards the registration state below. Registration and teardown both
        // run on pool threads, but they are kicked off from different places
        // (the constructor and the destructor) and can interleave in either
        // order, so the pair has to be serialised: without this, a subscription
        // destroyed while its manager was still being acquired would end up
        // registered with no one left to ever unregister it.
        std::mutex mutex;
        bool registered = false;
        Manager manager{nullptr};
        winrt::event_token currentSessionToken{};
        winrt::event_token sessionsChangedToken{};

        // The handlers behind those two tokens, and the per-session registrations.
        // All of it is cleared only after remove_ has run.
        CurrentSessionHandler currentSessionHandler{};
        SessionsChangedHandler sessionsChangedHandler{};
        std::vector<Watched> watched;

        void Notify() const {
            if (!alive.load()) return;
            // A bare envelope is the "something changed, go re-read the world"
            // signal: no payload, so even a chatty event source only costs one
            // small allocation that the receiver frees on the spot.
            PostOrDelete(notifyWindow, notifyMessage, new AsyncEnvelope());
        }
    };

    // Handler types, spelled out because the registrations have to be stored
    // somewhere with a name. The bodies do not care about the arguments at all -
    // they only need to know that something changed - so each is a variadic
    // lambda that forwards to Notify().
    static auto OnChange(std::weak_ptr<State> weak) {
        return [weak](auto&&...) {
            if (auto state = weak.lock()) state->Notify();
        };
    }

    // PlaybackInfoChanged covers play/pause/skip; MediaPropertiesChanged covers
    // a track change within the same app. Windows exposes neither on the manager
    // itself - only CurrentSessionChanged and SessionsChanged - so per-app state
    // has to be watched on the session.
    static void WatchSession(const std::shared_ptr<State>& state, WinRTSession session) {
        if (!session) return;
        auto weak = std::weak_ptr<State>(state);

        // Build the handlers, register them, then move the whole set into the
        // watched list. TypedEventHandler has no default constructor, so the
        // entry is aggregate-initialised rather than filled in field by field.
        PlaybackHandler playback(OnChange(weak));
        MediaPropsHandler properties(OnChange(weak));
        const auto playbackToken = session.PlaybackInfoChanged(playback);
        const auto propertiesToken = session.MediaPropertiesChanged(properties);

        state->watched.push_back(Watched{session, playbackToken, propertiesToken,
                                         std::move(playback), std::move(properties)});
    }

    static winrt::fire_and_forget RegisterAsync(std::shared_ptr<State> state) {
        co_await winrt::resume_background();
        Manager manager = co_await Manager::RequestAsync();

        auto weak = std::weak_ptr<State>(state);
        CurrentSessionHandler currentHandler(OnChange(weak));
        SessionsChangedHandler sessionsHandler(OnChange(weak));
        auto currentToken = manager.CurrentSessionChanged(currentHandler);
        auto sessionsToken = manager.SessionsChanged(sessionsHandler);

        // Watch every session that exists right now, not just the current one:
        // a background app starting to play is exactly the change a panel that
        // lists all apps needs to hear about.
        bool stillWanted = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->alive.load()) {
                for (auto const& session : manager.GetSessions()) WatchSession(state, session);
                stillWanted = true;
            }
        }
        if (!stillWanted) {
            // Died while we were acquiring: drop the two manager registrations
            // we just made and leave nothing behind. The handlers are locals, so
            // they die with this frame - after the tokens, which is the order
            // that matters.
            manager.CurrentSessionChanged(currentToken);
            manager.SessionsChanged(sessionsToken);
            co_return;
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->manager = std::move(manager);
            state->currentSessionToken = currentToken;
            state->sessionsChangedToken = sessionsToken;
            state->currentSessionHandler = currentHandler;
            state->sessionsChangedHandler = sessionsHandler;
            state->registered = true;
        }
    }

    static winrt::fire_and_forget UnregisterAsync(std::shared_ptr<State> state) {
        co_await winrt::resume_background();
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->registered) {
            co_return;
        }

        // remove_ first, release the handlers second - never the other way
        // round. Dropping a handler while its token is still live is precisely
        // the dangling registration described on Watched.
        state->manager.CurrentSessionChanged(state->currentSessionToken);
        state->manager.SessionsChanged(state->sessionsChangedToken);

        for (auto& w : state->watched) {
            w.session.PlaybackInfoChanged(w.playbackToken);
            w.session.MediaPropertiesChanged(w.propertiesToken);
        }

        // Only now, with nothing registered anywhere, can the handlers go.
        state->watched.clear();
        state->currentSessionHandler = nullptr;
        state->sessionsChangedHandler = nullptr;
        state->currentSessionToken = {};
        state->sessionsChangedToken = {};
        state->manager = nullptr;
        state->registered = false;
    }

    std::shared_ptr<State> m_state;
};

} // namespace

winrt::fire_and_forget RefreshAsync(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
                                    std::uint64_t listGeneration) {
    auto* list = new SessionList();
    list->requestId = requestId;
    list->listGeneration = listGeneration;
    try {
        co_await winrt::resume_background();
        Manager manager = co_await Manager::RequestAsync();

        for (auto const& session : manager.GetSessions()) {
            list->infos.push_back(DescribeSession(session));

            // Title/artist cost one round trip to the app each, so they are
            // awaited here rather than on the cheap transport-state path above.
            // A failure only costs the row its subtitle.
            try {
                auto props = co_await session.TryGetMediaPropertiesAsync();
                if (props) {
                    list->infos.back().title = props.Title().c_str();
                    list->infos.back().artist = props.Artist().c_str();
                }
            } catch (...) {
            }

            list->handles.push_back(std::make_shared<ConcreteSessionHandle>(session));
        }
    } catch (...) {
        // Leave `list` with whatever was gathered before the failure
        // (possibly nothing) - a partial/empty result is fine here.
    }
    PostOrDelete(notifyWindow, notifyMessage, list);
}

winrt::fire_and_forget SendCommandAsync(std::shared_ptr<SessionHandle> handle, Command command,
                                        int tag, std::uint64_t requestId,
                                        std::uint64_t listGeneration, HWND notifyWindow,
                                        UINT notifyMessage) {
    auto* result = new CommandResult();
    result->requestId = requestId;
    result->tag = tag;
    result->listGeneration = listGeneration;

    Result outcome = Result::Failed;
    try {
        co_await winrt::resume_background();
        auto* concrete = static_cast<ConcreteSessionHandle*>(handle.get());
        WinRTSession session = concrete->session;
        auto controls = session.GetPlaybackInfo().Controls();

        bool supported = false;
        bool succeeded = false;
        switch (command) {
            case Command::Previous:
                supported = controls.IsPreviousEnabled();
                if (supported) succeeded = co_await session.TrySkipPreviousAsync();
                break;
            case Command::PlayPause:
                supported = controls.IsPlayPauseToggleEnabled();
                if (supported) succeeded = co_await session.TryTogglePlayPauseAsync();
                break;
            case Command::Next:
                supported = controls.IsNextEnabled();
                if (supported) succeeded = co_await session.TrySkipNextAsync();
                break;
        }
        outcome = !supported ? Result::Unsupported : (succeeded ? Result::Success : Result::Failed);
    } catch (...) {
        outcome = Result::Failed;
    }
    result->code = static_cast<int>(outcome);
    PostOrDelete(notifyWindow, notifyMessage, result);
}

winrt::fire_and_forget QueryNowPlayingAsync(HWND notifyWindow, UINT notifyMessage,
                                            std::uint64_t requestId) {
    auto* now = new NowPlaying();
    now->requestId = requestId;
    try {
        co_await winrt::resume_background();
        Manager manager = co_await Manager::RequestAsync();
        WinRTSession session = manager.GetCurrentSession();
        if (session) {
            now->hasSession = true;
            winrt::hstring aumid = session.SourceAppUserModelId();
            now->sourceId.assign(aumid.c_str(), aumid.size());
            now->isPlaying = session.GetPlaybackInfo().PlaybackStatus() ==
                             GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            now->positionMs = session.GetTimelineProperties().Position().count() / 10000;
            now->durationMs = session.GetTimelineProperties().EndTime().count() / 10000;

            auto props = co_await session.TryGetMediaPropertiesAsync();
            if (props) {
                now->title = props.Title().c_str();
                now->artist = props.Artist().c_str();
            }
        }
    } catch (...) {
        // A failed probe is reported as "no session"; the caller re-queries.
    }
    PostOrDelete(notifyWindow, notifyMessage, now);
}

void RefreshSessions(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
                     std::uint64_t listGeneration) {
    RefreshAsync(notifyWindow, notifyMessage, requestId, listGeneration);
}

void SendCommand(std::shared_ptr<SessionHandle> session, Command command, int tag,
                 std::uint64_t requestId, std::uint64_t listGeneration, HWND notifyWindow,
                 UINT notifyMessage) {
    SendCommandAsync(std::move(session), command, tag, requestId, listGeneration, notifyWindow,
                     notifyMessage);
}

void QueryNowPlaying(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId) {
    QueryNowPlayingAsync(notifyWindow, notifyMessage, requestId);
}

std::shared_ptr<MediaChangeSubscription> SubscribeToChanges(HWND notifyWindow, UINT notifyMessage) {
    return std::make_shared<ChangeSubscription>(notifyWindow, notifyMessage);
}

} // namespace MediaControls

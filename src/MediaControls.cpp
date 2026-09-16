#include "MediaControls.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

namespace MediaControls {
namespace {

using WinRTSession = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using Manager = winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

constexpr int kTagShift = 8;

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

winrt::fire_and_forget RefreshAsync(HWND notifyWindow, UINT notifyMessage) {
    auto* list = new SessionList();
    try {
        co_await winrt::resume_background();
        Manager manager = co_await Manager::RequestAsync();

        for (auto const& session : manager.GetSessions()) {
            SessionInfo info;
            info.displayName = PrettyName(session.SourceAppUserModelId());
            auto controls = session.GetPlaybackInfo().Controls();
            info.canPrevious = controls.IsPreviousEnabled();
            info.canPlayPause = controls.IsPlayPauseToggleEnabled();
            info.canNext = controls.IsNextEnabled();
            info.isPlaying = session.GetPlaybackInfo().PlaybackStatus() ==
                              GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

            list->infos.push_back(std::move(info));
            list->handles.push_back(std::make_shared<ConcreteSessionHandle>(session));
        }
    } catch (...) {
        // Leave `list` with whatever was gathered before the failure
        // (possibly nothing) - a partial/empty result is fine here.
    }
    PostMessageW(notifyWindow, notifyMessage, reinterpret_cast<WPARAM>(list), 0);
}

winrt::fire_and_forget SendCommandAsync(std::shared_ptr<SessionHandle> handle, Command command,
                                         int tag, HWND notifyWindow, UINT notifyMessage) {
    Result result = Result::Failed;
    try {
        co_await winrt::resume_background();
        auto* concrete = static_cast<ConcreteSessionHandle*>(handle.get());
        auto session = concrete->session;
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
        result = !supported ? Result::Unsupported : (succeeded ? Result::Success : Result::Failed);
    } catch (...) {
        result = Result::Failed;
    }
    WPARAM packed = (static_cast<WPARAM>(tag) << kTagShift) | static_cast<WPARAM>(result);
    PostMessageW(notifyWindow, notifyMessage, packed, 0);
}

} // namespace

void RefreshSessions(HWND notifyWindow, UINT notifyMessage) {
    RefreshAsync(notifyWindow, notifyMessage);
}

void SendCommand(std::shared_ptr<SessionHandle> session, Command command, int tag,
                  HWND notifyWindow, UINT notifyMessage) {
    SendCommandAsync(std::move(session), command, tag, notifyWindow, notifyMessage);
}

void UnpackResult(WPARAM wParam, int& outTag, Result& outResult) {
    outTag = static_cast<int>(wParam >> kTagShift);
    outResult = static_cast<Result>(wParam & 0xFF);
}

} // namespace MediaControls

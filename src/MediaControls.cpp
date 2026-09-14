#include "MediaControls.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <string_view>

namespace MediaControls {
namespace {

using Manager = winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;

bool IsSpotifySession(winrt::hstring const& source) {
    std::wstring_view name{source.c_str(), source.size()};
    return name.find(L"Spotify") != std::wstring_view::npos ||
           name.find(L"spotify") != std::wstring_view::npos;
}

winrt::fire_and_forget SendAsync(SpotifyCommand command, HWND notifyWindow, UINT notifyMessage) {
    Result result = Result::Failed;
    try {
        co_await winrt::resume_background();
        Manager manager = co_await Manager::RequestAsync();
        result = Result::SpotifyNotFound;

        for (auto const& session : manager.GetSessions()) {
            if (!IsSpotifySession(session.SourceAppUserModelId())) continue;

            auto controls = session.GetPlaybackInfo().Controls();
            bool supported = false;
            bool succeeded = false;
            switch (command) {
                case SpotifyCommand::Previous:
                    supported = controls.IsPreviousEnabled();
                    if (supported) succeeded = co_await session.TrySkipPreviousAsync();
                    break;
                case SpotifyCommand::PlayPause:
                    supported = controls.IsPlayPauseToggleEnabled();
                    if (supported) succeeded = co_await session.TryTogglePlayPauseAsync();
                    break;
                case SpotifyCommand::Next:
                    supported = controls.IsNextEnabled();
                    if (supported) succeeded = co_await session.TrySkipNextAsync();
                    break;
            }
            result = !supported ? Result::Unsupported :
                     (succeeded ? Result::Success : Result::Failed);
            break;
        }
    } catch (...) {
        result = Result::Failed;
    }
    PostMessageW(notifyWindow, notifyMessage, static_cast<WPARAM>(result), 0);
}

} // namespace

void SendSpotifyCommand(SpotifyCommand command, HWND notifyWindow, UINT notifyMessage) {
    SendAsync(command, notifyWindow, notifyMessage);
}

} // namespace MediaControls

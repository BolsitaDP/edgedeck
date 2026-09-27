#pragma once

#include "AsyncResult.h"
#include "LrcLyrics.h"
#include "MediaControls.h"
#include "PanelWidget.h"

#include <memory>
#include <string>
#include <vector>

// Shows synced lyrics for whatever is currently playing (via LRCLIB - free,
// public, no account needed, not tied to Spotify).
//
// The lyrics themselves are fetched when the panel opens, but the *position*
// within the track is sampled while the panel is on screen, so the highlight
// follows the song instead of freezing at wherever it happened to be. That
// sampling is the one thing Windows offers no event for, so it runs from
// PanelWidget::OnTick - and Tab only ticks while a panel is actually visible,
// so a closed EdgeDeck still has no timers at all.
class LyricsWidget : public PanelWidget {
public:
    ~LyricsWidget() override;

    WidgetType Type() const override { return WidgetType::Lyrics; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 220.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float /*x*/, float /*y*/, float /*width*/, float /*contentHeight*/) const override {
        return -1;
    }
    void SetHovered(int /*controlId*/) override {}
    void Activate(int /*controlId*/, HWND /*ownerHwnd*/) override {}
    void OnAsyncResult(UINT message, WPARAM wParam) override;
    void OnPanelOpening(HWND ownerHwnd) override;

    bool WantsTicks() const override { return m_state == State::Ready; }
    void OnTick() override;

    std::wstring AccessibleSummary() const override;

    static constexpr UINT kFetchMessage = kWidgetMessageFirst + 21;
    static constexpr UINT kNowPlayingMessage = kWidgetMessageFirst + 22;

    // A separate id from kNowPlayingMessage, deliberately. The change
    // subscription posts a bare AsyncEnvelope; routing it through the
    // NowPlaying handler made the handler read a NowPlaying that was not there.
    static constexpr UINT kChangedMessage = kWidgetMessageFirst + 23;

private:
    enum class State { Loading, Ready, Empty };

    void UpdateCurrentLine(int positionMs);
    void ApplyTrack(const std::wstring& trackKey, std::vector<LrcLyrics::Line> lines, int positionMs);
    void ShowEmpty(LrcLyrics::Status status);

    State m_state = State::Loading;
    LrcLyrics::Status m_lastStatus = LrcLyrics::Status::Success;
    std::wstring m_trackKey;
    std::vector<LrcLyrics::Line> m_lines;
    int m_currentLine = -1; // index into m_lines closest to the last known position

    HWND m_owner = nullptr;
    RequestTracker m_requests;
    std::uint64_t m_fetchRequestId = 0;
    std::uint64_t m_nowPlayingRequestId = 0;
    std::shared_ptr<MediaControls::MediaChangeSubscription> m_subscription;
};

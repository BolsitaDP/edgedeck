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
    const wchar_t* TabIcon() const override;
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
    void OnPanelVisibilityChanged(bool visible) override { m_visible = visible; }

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
    void ApplyTrack(const LrcLyrics::Result& result);
    // The first line shown: around the current one for synced lyrics, and for plain ones - which
    // have no times - as far down as the song is through, so the text keeps up with it.
    int FirstShownLine() const;
    // The track's name, and for plain lyrics a second line saying they are not synced.
    float HeaderHeight() const { return m_synced ? 24.0f : 48.0f; }
    void ShowEmpty(LrcLyrics::Status status);

    State m_state = State::Loading;
    LrcLyrics::Status m_lastStatus = LrcLyrics::Status::Success;
    std::wstring m_trackKey;
    std::vector<LrcLyrics::Line> m_lines;
    int m_currentLine = -1; // index into m_lines closest to the last known position
    bool m_synced = true;   // m_lines carry times
    int m_positionMs = 0;
    int m_durationMs = 0;
    // Media change events come whether or not the panel is open: they are acted on only while it
    // is, since opening it looks again anyway. And after a failed lookup, another is not tried
    // for kRetryAfterFailureMs, so a flaky network is not asked again on every pause and skip.
    bool m_visible = false;
    ULONGLONG m_failedAt = 0;
    static constexpr ULONGLONG kRetryAfterFailureMs = 30000;

    HWND m_owner = nullptr;
    RequestTracker m_requests;
    std::uint64_t m_fetchRequestId = 0;
    std::uint64_t m_nowPlayingRequestId = 0;
    std::shared_ptr<MediaControls::MediaChangeSubscription> m_subscription;
};

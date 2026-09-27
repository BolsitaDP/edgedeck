#pragma once

#include "AsyncResult.h"
#include "MediaControls.h"
#include "PanelWidget.h"

#include <memory>
#include <vector>

// One row per currently-active media session (Spotify, a browser tab, VLC,
// ...), not tied to a single app. Each row shows the app, the track, a
// progress line and the transport controls.
//
// Freshness comes from two sources, neither of which is a poll:
//  * OnPanelOpening re-enumerates, because a panel that was closed for an
//    hour must not show an hour-old list.
//  * A GlobalSystemMediaTransportControlsSessionManager subscription posts a
//    cheap "something changed" notice, so starting a track, pausing or
//    skipping updates the open panel immediately.
//
// The row count drives the panel's own height via PreferredContentHeight.
class MediaWidget : public PanelWidget {
public:
    ~MediaWidget() override;

    WidgetType Type() const override { return WidgetType::Media; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 268.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hoveredControl = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;
    void OnAsyncResult(UINT message, WPARAM wParam) override;
    void OnPanelOpening(HWND ownerHwnd) override;
    void OnPanelVisibilityChanged(bool visible) override;

    bool WantsTicks() const override { return m_state == State::Ready; }
    void OnTick() override;

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return static_cast<int>(m_rows.size()) * 3; }
    int FocusedControl() const override { return m_focusedControl; }
    void SetFocusedControl(int controlId) override { m_focusedControl = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;

    static constexpr UINT kCommandResultMessage = kWidgetMessageFirst + 1;
    static constexpr UINT kRefreshMessage = kWidgetMessageFirst + 2;
    static constexpr UINT kChangedMessage = kWidgetMessageFirst + 3;
    static constexpr UINT kNowPlayingMessage = kWidgetMessageFirst + 4;

private:
    enum class State { Idle, Ready, Empty };

    struct Row {
        MediaControls::SessionInfo info;
        std::shared_ptr<MediaControls::SessionHandle> handle;
        std::uint64_t commandRequestId = 0; // 0 = no command in flight for this row
    };

    void RequestRefresh(HWND ownerHwnd);
    void EnsureSubscription(HWND ownerHwnd);

    std::vector<Row> m_rows;
    State m_state = State::Idle;
    int m_hoveredControl = -1;
    int m_focusedControl = -1;

    // Bumped every time a refresh is started. A reply carries the generation it
    // was issued against, so a reply that arrives after a newer refresh can
    // never flip the play state of whichever app now sits in that row index.
    std::uint64_t m_listGeneration = 0;
    RequestTracker m_requests;
    std::uint64_t m_refreshRequestId = 0;
    std::uint64_t m_nowPlayingRequestId = 0;
    ULONGLONG m_lastRefreshTick = 0; // throttles chatty change events

    std::shared_ptr<MediaControls::MediaChangeSubscription> m_subscription;
    HWND m_owner = nullptr;
};

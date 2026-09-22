#pragma once

#include "PanelWidget.h"
#include "LrcLyrics.h"
#include <vector>

// Shows synced lyrics for whatever is currently playing (via LRCLIB - free,
// public, no account needed, not tied to Spotify). Fetched only when the
// panel opens (OnPanelOpening) - no timer, so the displayed position is a
// snapshot from that moment, not a live-following karaoke view; that
// trade-off keeps this at zero idle cost like every other widget here.
// Non-interactive: no buttons, no drag.
class LyricsWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::Lyrics; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float /*x*/, float /*y*/, float /*width*/, float /*contentHeight*/) const override {
        return -1;
    }
    void SetHovered(int /*controlId*/) override {}
    void Activate(int /*controlId*/, HWND /*ownerHwnd*/) override {}
    void OnAsyncResult(UINT message, WPARAM wParam) override;
    void OnPanelOpening(HWND ownerHwnd) override;

    static constexpr UINT kFetchMessage = WM_APP + 21;

private:
    enum class State { Loading, Ready, Empty };

    State m_state = State::Loading;
    LrcLyrics::Status m_lastStatus = LrcLyrics::Status::Success;
    std::wstring m_trackKey;
    std::vector<LrcLyrics::Line> m_lines;
    int m_currentLine = -1; // index into m_lines closest to the fetched position
};

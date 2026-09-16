#pragma once

#include "PanelWidget.h"
#include "MediaControls.h"
#include <vector>
#include <memory>

// One row per currently-active media session (Spotify, a browser tab, VLC,
// ...) - not tied to a single app. Refreshes its list only when the panel
// is about to open (PanelWidget::OnPanelOpening), never on a timer, so idle
// cost stays zero; the row count then drives the panel's own height via
// PreferredContentHeight.
class MediaWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::Media; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hoveredControl = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;
    void OnAsyncResult(UINT message, WPARAM wParam) override;
    void OnPanelOpening(HWND ownerHwnd) override;

    static constexpr UINT kCommandResultMessage = WM_APP + 1;
    static constexpr UINT kRefreshMessage = WM_APP + 2;

private:
    struct Row {
        MediaControls::SessionInfo info;
        std::shared_ptr<MediaControls::SessionHandle> handle;
        bool busy = false;
    };

    std::vector<Row> m_rows;
    bool m_hasLoaded = false; // false until the first refresh reply arrives
    int m_hoveredControl = -1;
};

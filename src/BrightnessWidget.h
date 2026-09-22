#pragma once

#include "PanelWidget.h"
#include "BrightnessControls.h"
#include <memory>
#include <vector>

// One row per attached monitor: its name and a brightness slider. The
// monitor list is re-read only when the panel opens (OnPanelOpening). A
// slider updates its on-screen value while dragged, but the DDC/CI write is
// sent only when the button is released (one write per adjustment).
class BrightnessWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::Brightness; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hoveredRow = controlId; }
    void Activate(int /*controlId*/, HWND /*ownerHwnd*/) override {}
    void OnAsyncResult(UINT message, WPARAM wParam) override;
    void OnPanelOpening(HWND ownerHwnd) override;

    bool OnDragBegin(float x, float y, float width, float contentHeight, HWND ownerHwnd) override;
    void OnDragMove(float x, float y, float width, float contentHeight) override;
    void OnDragEnd() override;

    static constexpr UINT kRefreshMessage = WM_APP + 11;
    static constexpr UINT kSetResultMessage = WM_APP + 12;

private:
    struct Row {
        BrightnessControls::MonitorInfo info;
        std::shared_ptr<BrightnessControls::MonitorHandle> handle;
        int sentPercent = 0;
        bool inFlight = false;
    };

    int RowAtY(float y) const;
    void ApplyPointer(int row, float x, float width);
    void Pump(int row);

    std::vector<Row> m_rows;
    bool m_hasLoaded = false;
    int m_hoveredRow = -1;
    int m_dragRow = -1;
    HWND m_owner = nullptr;
};

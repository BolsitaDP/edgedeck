#pragma once

#include "AsyncResult.h"
#include "BrightnessControls.h"
#include "PanelWidget.h"

#include <memory>
#include <string>
#include <vector>

// One row per attached monitor: its name and a brightness slider. The monitor
// list is re-read only when the panel opens (OnPanelOpening). A slider updates
// its on-screen value while dragged, but the DDC/CI write is sent only when the
// button is released (one write per adjustment).
class BrightnessWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::Brightness; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 240.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hoveredControl = controlId; }
    void Activate(int /*controlId*/, HWND /*ownerHwnd*/) override {}
    void OnAsyncResult(UINT message, WPARAM wParam) override;
    void OnPanelOpening(HWND ownerHwnd) override;

    bool OnDragBegin(float x, float y, float width, float contentHeight, HWND ownerHwnd) override;
    void OnDragMove(float x, float y, float width, float contentHeight) override;
    void OnDragEnd() override;

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return static_cast<int>(m_rows.size()); }
    int FocusedControl() const override { return m_focusedControl; }
    void SetFocusedControl(int controlId) override { m_focusedControl = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;
    bool OnStepControl(int controlId, int direction, float& outValue01) override;

    static constexpr UINT kRefreshMessage = kWidgetMessageFirst + 11;
    static constexpr UINT kSetResultMessage = kWidgetMessageFirst + 12;

private:
    struct Row {
        BrightnessControls::MonitorInfo info;
        std::shared_ptr<BrightnessControls::MonitorHandle> handle;
        int sentPercent = -1;     // last value actually written, -1 = never
        int confirmedPercent = 0; // last value the monitor confirmed
        bool writeFailed = false; // last write was rejected by the hardware
        std::uint64_t writeRequestId = 0; // 0 = no write in flight
    };

    int RowAtY(float y) const;
    bool ApplyPointer(int row, float x, float width);
    void Send(int row);
    void SnapTo(int row, int percent);

    std::vector<Row> m_rows;
    bool m_hasLoaded = false;
    int m_hoveredControl = -1;
    int m_focusedControl = -1;
    int m_dragRow = -1;
    HWND m_owner = nullptr;

    std::uint64_t m_listGeneration = 0;
    RequestTracker m_requests;
    std::uint64_t m_refreshRequestId = 0;
};

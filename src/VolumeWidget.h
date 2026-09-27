#pragma once

#include "PanelWidget.h"
#include "VolumeControls.h"

#include <memory>
#include <string>
#include <vector>

// One row per active output device: name, a volume slider and a mute toggle.
// Unlike the brightness widget, Core Audio volume is an in-process call, so the
// slider writes on every change (no deferred write) and the panel re-reads
// while it is open, which is what keeps it in step with volume keys pressed in
// another app.
class VolumeWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::Volume; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 250.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hovered = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;
    void OnPanelOpening(HWND ownerHwnd) override;
    void OnPanelVisibilityChanged(bool visible) override;

    bool OnDragBegin(float x, float y, float width, float contentHeight, HWND ownerHwnd) override;
    void OnDragMove(float x, float y, float width, float contentHeight) override;
    void OnDragEnd() override;

    bool WantsTicks() const override { return true; }
    void OnTick() override;

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return static_cast<int>(m_rows.size()) * 2; }
    int FocusedControl() const override { return m_focused; }
    void SetFocusedControl(int controlId) override { m_focused = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;
    bool OnStepControl(int controlId, int direction, float& outValue01) override;

private:
    struct Row {
        VolumeControls::DeviceInfo info;
        std::shared_ptr<VolumeControls::DeviceHandle> handle;
    };

    static constexpr int kMuteButton = 1; // control id within a row: 0 = slider, 1 = mute

    float RowTop(int row) const;
    int RowAtY(float y) const;
    void Refresh();
    bool Commit(int row, int percent, bool muted);

    std::vector<Row> m_rows;
    bool m_hasLoaded = false;
    int m_hovered = -1;
    int m_focused = -1;
    int m_dragRow = -1;
};

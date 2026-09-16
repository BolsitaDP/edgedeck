#pragma once

#include "PanelWidget.h"

// The original three test actions (Notepad / Calculator / Show Desktop),
// now living behind the PanelWidget interface instead of hardcoded into Tab.
class QuickActionsWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::QuickActions; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hovered = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;

private:
    int m_hovered = -1;
};

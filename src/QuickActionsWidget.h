#pragma once

#include "PanelWidget.h"

#include <string>

// The original three test actions (Notepad / Calculator / Show Desktop),
// now living behind the PanelWidget interface instead of hardcoded into Tab.
class QuickActionsWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::QuickActions; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* TabIcon() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 180.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hovered = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;
    void OnPanelOpening(HWND ownerHwnd) override { m_owner = ownerHwnd; }

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return kRowCount; }
    int FocusedControl() const override { return m_focused; }
    void SetFocusedControl(int controlId) override { m_focused = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;

    static constexpr int kRowCount = 3;

private:
    int m_hovered = -1;
    int m_focused = -1;
    // The tab window, remembered from when the panel opened. The keyboard route
    // activates a row with no window to hand over, and a failure has to be
    // reported to somebody.
    HWND m_owner = nullptr;
};

#pragma once

#include "PanelWidget.h"
#include "QuickAction.h"

#include <string>
#include <vector>

// A column of rows, each one starting something: the user's own list (Config::QuickActionList),
// which is Notepad, Calculator and Show Desktop until they change it. The list is copied when the
// widget is made and again when the settings say it changed (OnSettingsChanged), so painting and
// hit-testing never wait on the config.
class QuickActionsWidget : public PanelWidget {
public:
    QuickActionsWidget();

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
    void OnSettingsChanged() override;

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return static_cast<int>(m_actions.size()); }
    int FocusedControl() const override { return m_focused; }
    void SetFocusedControl(int controlId) override { m_focused = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;

private:
    int RowCount() const { return static_cast<int>(m_actions.size()); }

    std::vector<QuickAction> m_actions;
    int m_hovered = -1;
    int m_focused = -1;
    // The tab window, remembered from when the panel opened. The keyboard route
    // activates a row with no window to hand over, and a failure has to be
    // reported to somebody.
    HWND m_owner = nullptr;
};

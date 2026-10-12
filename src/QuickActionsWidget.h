#pragma once

#include "AsyncResult.h"
#include "PanelWidget.h"
#include "QuickAction.h"

#include <memory>
#include <string>
#include <vector>

// A column of rows, each one starting something: the user's own list (Config::QuickActionList),
// which is Notepad, Calculator and Show Desktop until they change it. The list is copied when the
// widget is made and again when the settings say it changed (OnSettingsChanged), so painting and
// hit-testing never wait on the config.
//
// Each row carries the picture Explorer shows for what it opens. They are looked up the first time
// the panel opens - off the UI thread, see ActionIcons - and kept; a row whose target has no
// picture (the built-in "show desktop", a file that is gone) shows a lettered badge in the same
// place, so the names line up either way.
class QuickActionsWidget : public PanelWidget {
public:
    QuickActionsWidget();

    WidgetType Type() const override { return WidgetType::QuickActions; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* TabIcon() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 210.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hovered = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;
    void OnPanelOpening(HWND ownerHwnd) override;
    void OnSettingsChanged() override;
    void OnAsyncResult(UINT message, WPARAM wParam) override;

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return static_cast<int>(m_actions.size()); }
    int FocusedControl() const override { return m_focused; }
    void SetFocusedControl(int controlId) override { m_focused = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;

private:
    static constexpr UINT kIconsMessage = kWidgetMessageFirst + 51;

    // Where a row's picture stands. Unknown has not been asked for, Pending is on its way; Ready
    // with no image means the answer was "nothing to show" and the badge is drawn.
    struct Icon {
        enum class State { Unknown, Pending, Ready } state = State::Unknown;
        std::shared_ptr<const PanelImage> image;
    };

    int RowCount() const { return static_cast<int>(m_actions.size()); }
    void ResetIcons(const std::vector<std::wstring>& previousTargets,
                    std::vector<Icon>&& previousIcons);
    void RequestIcons();

    std::vector<QuickAction> m_actions;
    // The targets with their %Variables% expanded, in step with m_actions and m_icons: what a
    // reply is matched against, and what is asked of the shell.
    std::vector<std::wstring> m_iconTargets;
    std::vector<Icon> m_icons;
    RequestTracker m_requests;
    int m_hovered = -1;
    int m_focused = -1;
    // The tab window, remembered from when the panel opened. The keyboard route
    // activates a row with no window to hand over, and a failure has to be
    // reported to somebody; the picture lookup answers to it as well.
    HWND m_owner = nullptr;
};

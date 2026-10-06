#pragma once

#include "AsyncResult.h"
#include "DisplayControls.h"
#include "DisplayTopology.h"
#include "PanelWidget.h"

#include <string>
#include <vector>

// Three buttons that decide which monitors are switched on: the two desktop
// monitors, the TV alone, or all of them. The button matching what is on right
// now is marked, so the panel doubles as a read-out of the current layout.
//
// The monitor list is read when the panel opens and again after a switch - there
// is no polling. Switching runs on the thread pool because Windows holds the call
// for as long as the panels take to resync.
class DisplayWidget : public PanelWidget {
public:
    WidgetType Type() const override { return WidgetType::Displays; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 240.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hoveredControl = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;
    void OnAsyncResult(UINT message, WPARAM wParam) override;
    void OnPanelOpening(HWND ownerHwnd) override;

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return kProfileCount; }
    int FocusedControl() const override { return m_focusedControl; }
    void SetFocusedControl(int controlId) override { m_focusedControl = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;

    static constexpr int kProfileCount = 3;
    static constexpr UINT kRefreshMessage = kWidgetMessageFirst + 41;
    static constexpr UINT kApplyMessage = kWidgetMessageFirst + 42;

private:
    DisplayTopology::ProfileState StateOf(int profileIndex) const;
    std::wstring Subtitle(int profileIndex, const DisplayTopology::ProfileState& state) const;
    // supersede: a refresh already in flight is abandoned (its reply will be
    // dropped as stale) and a fresh one is started, which is what is wanted right
    // after a switch - the older read predates it.
    void RequestRefresh(HWND ownerHwnd, bool supersede = false);

    std::vector<DisplayTopology::Monitor> m_monitors;
    std::wstring m_tvId;
    bool m_loaded = false;

    int m_hoveredControl = -1;
    int m_focusedControl = -1;
    HWND m_owner = nullptr;

    RequestTracker m_requests;
    std::uint64_t m_refreshRequestId = 0;
    std::uint64_t m_applyRequestId = 0; // 0 = nothing being switched
    int m_applyingRow = -1;
};

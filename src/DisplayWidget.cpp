#include "DisplayWidget.h"

#include "Config.h"
#include "Renderer.h"
#include "Strings.h"

#include <algorithm>
#include <cstdio>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 52.0f;
constexpr float kRowGap = 4.0f;
constexpr float kBadgeSize = 32.0f;
constexpr float kValueWidth = 78.0f;
constexpr float kNameHeight = 34.0f;
constexpr float kMessageHeight = 44.0f;

// U+25A1 WHITE SQUARE - a screen, near enough, and inside the WGL4 set Segoe UI
// is guaranteed to carry. A numeric code point rather than a literal character so
// the source stays ASCII.
constexpr wchar_t kTabGlyph[] = {0x25A1, 0};

D2D1_RECT_F RowRect(int row, float width) {
    const float top = static_cast<float>(row) * (kRowHeight + kRowGap);
    return D2D1::RectF(0.0f, top, width, top + kRowHeight);
}

D2D1_RECT_F BadgeRect(D2D1_RECT_F row) {
    const float top = row.top + (kRowHeight - kBadgeSize) / 2.0f;
    return D2D1::RectF(kPaddingX, top, kPaddingX + kBadgeSize, top + kBadgeSize);
}

D2D1_RECT_F ValueRect(D2D1_RECT_F row) {
    return D2D1::RectF(row.right - kPaddingX - kValueWidth, row.top, row.right - kPaddingX,
                       row.bottom);
}

D2D1_RECT_F NameRect(D2D1_RECT_F row) {
    const float top = row.top + (kRowHeight - kNameHeight) / 2.0f;
    return D2D1::RectF(kPaddingX + kBadgeSize + 12.0f, top, ValueRect(row).left - 6.0f,
                       top + kNameHeight);
}
} // namespace

const wchar_t* DisplayWidget::TabGlyph() const { return kTabGlyph; }
// U+E7F4, a monitor.
const wchar_t* DisplayWidget::TabIcon() const {
    static constexpr wchar_t icon[] = {0xE7F4, 0};
    return icon;
}

const wchar_t* DisplayWidget::PanelTitle() const { return TR(DisplaysTitle); }

float DisplayWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (!m_loaded || m_layouts.empty()) return kMessageHeight;
    return static_cast<float>(RowCount()) * kRowHeight + static_cast<float>(RowCount() - 1) * kRowGap;
}

void DisplayWidget::ChooseLayouts() {
    std::vector<DisplayLayouts::Layout> saved = Config::DisplayLayoutList();
    m_automatic = saved.empty();
    m_layouts = m_automatic ? DisplayLayouts::Automatic(m_monitors) : std::move(saved);
    if (m_hoveredControl >= RowCount()) m_hoveredControl = -1;
    if (m_focusedControl >= RowCount()) m_focusedControl = -1;
}

DisplayLayouts::State DisplayWidget::StateOf(int row) const {
    if (!m_loaded || row < 0 || row >= RowCount()) return {};
    return DisplayLayouts::Evaluate(m_layouts[static_cast<size_t>(row)], m_monitors);
}

std::wstring DisplayWidget::Subtitle(int row, const DisplayLayouts::State& state) const {
    if (!m_loaded) return TR(Checking);
    if (!state.available) {
        // A saved layout names a monitor that is not plugged in: say which.
        return Strings::Format(Strings::Id::DisplaysMissing, DisplayLayouts::JoinIds(state.missing).c_str());
    }
    const DisplayLayouts::Layout& layout = m_layouts[static_cast<size_t>(row)];
    // Every monitor's name in one line does not fit beside the "Active" marker, and "all" is
    // better said as a count anyway.
    if (layout.ids.size() > 2) return Strings::Format(Strings::Id::DisplaysCount, layout.ids.size());
    return DisplayLayouts::NameFor(layout.ids, m_monitors);
}

void DisplayWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (!m_loaded || m_layouts.empty()) {
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kMessageHeight),
                          m_loaded ? TR(DisplaysNothingToSwitch) : TR(Checking), true);
        return;
    }

    const PanelTheme& theme = PanelTheme::Current();
    const bool switching = m_applyRequestId != 0;

    for (int i = 0; i < RowCount(); ++i) {
        const DisplayLayouts::State state = StateOf(i);
        const D2D1_RECT_F row = RowRect(i, width);

        // Only a button that would actually do something lights up under the
        // pointer: the active one is already the answer, and an unavailable one
        // has nothing to switch to.
        const bool clickable = state.available && !state.active && !switching;
        const bool highlighted = clickable && (i == m_hoveredControl || i == m_focusedControl);
        painter.DrawRow(D2D1::RectF(kPaddingX, row.top, width - kPaddingX, row.bottom), L"",
                        highlighted);

        // The badge is the layout's number, which is also its shortcut's digit.
        const std::wstring number = std::to_wstring(i + 1);
        painter.DrawBadge(BadgeRect(row), number.c_str(), state.active ? theme.accent : theme.controlBg);
        painter.DrawLabelPair(NameRect(row), m_layouts[static_cast<size_t>(i)].name.c_str(),
                              Subtitle(i, state).c_str());

        if (i == m_applyingRow) {
            painter.DrawValueText(ValueRect(row), TR(DisplaysSwitching), true);
        } else if (state.active) {
            painter.DrawValueText(ValueRect(row), TR(DisplaysActive), false);
        }
    }
}

int DisplayWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    if (x < 0.0f || x > width || y < 0.0f) return -1;
    const int row = static_cast<int>(y / (kRowHeight + kRowGap));
    if (row < 0 || row >= RowCount()) return -1;
    if (y > RowRect(row, width).bottom) return -1; // landed in the gap between rows
    return row;
}

void DisplayWidget::Activate(int controlId, HWND ownerHwnd) {
    if (controlId < 0 || controlId >= RowCount()) return;
    if (m_applyRequestId != 0) return; // one switch at a time; Windows is still busy

    const DisplayLayouts::State state = StateOf(controlId);
    if (!state.available || state.active) return;

    m_owner = ownerHwnd;
    m_applyingRow = controlId;
    m_applyRequestId = m_requests.Begin();
    DisplayControls::ApplyProfile(m_layouts[static_cast<size_t>(controlId)].ids, controlId, ownerHwnd,
                                  kApplyMessage, m_applyRequestId);
}

void DisplayWidget::OnShortcut(int controlId, HWND ownerHwnd) {
    if (controlId < 0) return;
    if (m_applyRequestId != 0) return; // one switch at a time; Windows is still busy

    // Nothing here can be trusted: the panel may never have been opened, or not
    // since a monitor was plugged in or Win+P changed the layout. Read the monitors
    // again, and act on that read - superseding any older one, which predates the
    // key press. Whether the layout exists is only known after it, too.
    m_owner = ownerHwnd;
    RequestRefresh(ownerHwnd, true);
    m_shortcutLayout = controlId;
    m_shortcutRefreshId = m_refreshRequestId;
}

void DisplayWidget::OnSettingsChanged() {
    // The saved layouts changed: lay them out against the monitors already known, and read the
    // monitors again if the panel is in use.
    if (m_loaded) ChooseLayouts();
    if (m_owner) RequestRefresh(m_owner, true);
}

void DisplayWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
    if (!envelope) return;

    // Whatever this widget did not ask for - a reply for an older panel open, or
    // for a widget instance Settings has since replaced - is dropped. Freeing it
    // on every path is what keeps the ownership rule in one place.
    auto discard = [&] { delete envelope; };

    if (message == kRefreshMessage) {
        if (envelope->Kind() != AsyncKind::Displays) return discard();
        auto* list = static_cast<DisplayControls::MonitorList*>(envelope);
        if (!m_requests.Accept(list->requestId)) return discard();
        m_refreshRequestId = 0;

        m_monitors = std::move(list->monitors);
        m_loaded = true;
        ChooseLayouts();

        // If a shortcut asked for this very read, carry it out now that the state
        // is fresh. Activate still applies its own rules - no such layout, already
        // active, not available - so a shortcut for the current layout does nothing.
        const bool forShortcut = m_shortcutLayout >= 0 && list->requestId == m_shortcutRefreshId;
        const int layout = m_shortcutLayout;
        if (forShortcut) {
            m_shortcutLayout = -1;
            m_shortcutRefreshId = 0;
        }
        discard();
        if (forShortcut) Activate(layout, m_owner);
        return;
    }

    if (message == kApplyMessage) {
        if (envelope->Kind() != AsyncKind::DisplayApply) return discard();
        auto* result = static_cast<DisplayControls::ApplyResult*>(envelope);
        if (!m_requests.Accept(result->requestId)) return discard();
        m_applyRequestId = 0;
        m_applyingRow = -1;

        // Whatever happened, what is on screen now is what the panel should show.
        if (m_owner) RequestRefresh(m_owner, true);

        using DisplayTopology::ApplyResult;
        if (result->outcome.result == ApplyResult::NotConnected) {
            discard();
            ReportProblem(m_owner, TR(DisplaysOneMissing));
            return;
        }
        if (result->outcome.result == ApplyResult::Failed) {
            const std::wstring text =
                Strings::Format(Strings::Id::DisplaysApplyFailed, static_cast<long>(result->outcome.error));
            discard();
            ReportProblem(m_owner, text);
            return;
        }
        return discard();
    }

    discard();
}

void DisplayWidget::OnPanelOpening(HWND ownerHwnd) {
    m_owner = ownerHwnd;
    RequestRefresh(ownerHwnd);
}

void DisplayWidget::RequestRefresh(HWND ownerHwnd, bool supersede) {
    if (m_refreshRequestId != 0) {
        if (!supersede) return; // one read at a time
        m_requests.Retire(m_refreshRequestId);
    }
    m_refreshRequestId = m_requests.Begin();
    DisplayControls::Refresh(ownerHwnd, kRefreshMessage, m_refreshRequestId,
                             Config::DisplayTvMonitor(), Config::DisplayMainMonitor());
}

std::wstring DisplayWidget::AccessibleSummary() const {
    std::wstring summary = TR(DisplaysSummary);
    for (int i = 0; i < RowCount(); ++i) {
        if (StateOf(i).active) {
            summary += L" ";
            summary += Strings::Format(Strings::Id::DisplaysSummaryActive, m_layouts[static_cast<size_t>(i)].name.c_str());
        }
    }
    return summary;
}

std::wstring DisplayWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0 || controlId >= RowCount()) return {};
    const DisplayLayouts::State state = StateOf(controlId);
    return Strings::Format(state.active ? Strings::Id::DisplaysRowActive : Strings::Id::DisplaysRow,
                           m_layouts[static_cast<size_t>(controlId)].name.c_str(), Subtitle(controlId, state).c_str());
}

bool DisplayWidget::OnKeyDown(UINT key, int focusedControl) {
    const int count = RowCount();
    if (count == 0) return false;
    switch (key) {
        case VK_UP:
            m_focusedControl = (focusedControl <= 0) ? count - 1 : focusedControl - 1;
            break;
        case VK_DOWN:
            m_focusedControl = (focusedControl < 0 || focusedControl >= count - 1) ? 0 : focusedControl + 1;
            break;
        case VK_HOME:
            m_focusedControl = 0;
            break;
        case VK_END:
            m_focusedControl = count - 1;
            break;
        case VK_SPACE:
        case VK_RETURN:
            if (focusedControl >= 0) Activate(focusedControl, m_owner);
            return true;
        default:
            return false;
    }
    m_hoveredControl = m_focusedControl;
    return true;
}

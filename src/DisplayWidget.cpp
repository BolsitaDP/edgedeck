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

struct ProfileRow {
    DisplayTopology::Profile profile;
    Strings::Id title; // in the current language when shown
    const wchar_t* badge;
};

constexpr ProfileRow kProfiles[DisplayWidget::kProfileCount] = {
    {DisplayTopology::Profile::MainMonitors, Strings::Id::ProfileMainMonitors, L"1+2"},
    {DisplayTopology::Profile::TvOnly, Strings::Id::ProfileTvOnly, L"TV"},
    {DisplayTopology::Profile::AllMonitors, Strings::Id::ProfileAll, L"ALL"},
    {DisplayTopology::Profile::MainOnly, Strings::Id::ProfileMainOnly, L"1"},
};

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
    return kProfileCount * kRowHeight + (kProfileCount - 1) * kRowGap;
}

DisplayTopology::ProfileState DisplayWidget::StateOf(int profileIndex) const {
    if (!m_loaded || profileIndex < 0 || profileIndex >= kProfileCount) return {};
    return DisplayTopology::Evaluate(kProfiles[profileIndex].profile, m_monitors, m_tvId, m_mainId);
}

std::wstring DisplayWidget::Subtitle(int profileIndex,
                                     const DisplayTopology::ProfileState& state) const {
    if (!m_loaded) return TR(Checking);
    if (!state.available) {
        switch (kProfiles[profileIndex].profile) {
            case DisplayTopology::Profile::MainMonitors:
                return TR(NoMonitors);
            case DisplayTopology::Profile::MainOnly: {
                // Either the main monitor is not there (a configured one, unplugged) or it is the
                // only one that is not the TV, so there is nothing else to turn off.
                const bool mainThere = std::any_of(
                    m_monitors.begin(), m_monitors.end(),
                    [&](const DisplayTopology::Monitor& m) { return m.id == m_mainId; });
                return mainThere ? TR(DisplaysNoOthers) : TR(DisplaysMainMissing);
            }
            default:
                return TR(DisplaysTvMissing);
        }
    }

    // Every monitor's name in one line does not fit beside the "Active" marker
    // (it was cut off mid-name), and "all" is better said as a count anyway.
    if (kProfiles[profileIndex].profile == DisplayTopology::Profile::AllMonitors) {
        return Strings::Format(Strings::Id::DisplaysCount, state.ids.size());
    }

    std::wstring names;
    for (const std::wstring& id : state.ids) {
        for (const DisplayTopology::Monitor& m : m_monitors) {
            if (m.id != id) continue;
            if (!names.empty()) names += L" + ";
            names += m.name;
            break;
        }
    }
    return names;
}

void DisplayWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    const PanelTheme& theme = PanelTheme::Current();
    const bool switching = m_applyRequestId != 0;

    for (int i = 0; i < kProfileCount; ++i) {
        const DisplayTopology::ProfileState state = StateOf(i);
        const D2D1_RECT_F row = RowRect(i, width);

        // Only a button that would actually do something lights up under the
        // pointer: the active one is already the answer, and an unavailable one
        // has nothing to switch to.
        const bool clickable = m_loaded && state.available && !state.active && !switching;
        const bool highlighted = clickable && (i == m_hoveredControl || i == m_focusedControl);
        painter.DrawRow(D2D1::RectF(kPaddingX, row.top, width - kPaddingX, row.bottom), L"",
                        highlighted);

        painter.DrawBadge(BadgeRect(row), kProfiles[i].badge,
                          state.active ? theme.accent : theme.controlBg);
        painter.DrawLabelPair(NameRect(row), Strings::Get(kProfiles[i].title), Subtitle(i, state).c_str());

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
    if (row < 0 || row >= kProfileCount) return -1;
    if (y > RowRect(row, width).bottom) return -1; // landed in the gap between rows
    return row;
}

void DisplayWidget::Activate(int controlId, HWND ownerHwnd) {
    if (controlId < 0 || controlId >= kProfileCount) return;
    if (m_applyRequestId != 0) return; // one switch at a time; Windows is still busy

    const DisplayTopology::ProfileState state = StateOf(controlId);
    if (!state.available || state.active) return;

    m_owner = ownerHwnd;
    m_applyingRow = controlId;
    m_applyRequestId = m_requests.Begin();
    DisplayControls::ApplyProfile(state.ids, controlId, ownerHwnd, kApplyMessage,
                                  m_applyRequestId);
}

void DisplayWidget::OnShortcut(int controlId, HWND ownerHwnd) {
    if (controlId < 0 || controlId >= kProfileCount) return;
    if (m_applyRequestId != 0) return; // one switch at a time; Windows is still busy

    // Nothing here can be trusted: the panel may never have been opened, or not
    // since a monitor was plugged in or Win+P changed the layout. Read the monitors
    // again, and act on that read - superseding any older one, which predates the
    // key press.
    m_owner = ownerHwnd;
    RequestRefresh(ownerHwnd, true);
    m_shortcutProfile = controlId;
    m_shortcutRefreshId = m_refreshRequestId;
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
        m_tvId = std::move(list->tvId);
        m_mainId = std::move(list->mainId);
        m_loaded = true;

        // If a shortcut asked for this very read, carry it out now that the state
        // is fresh. Activate still applies its own rules - already active, not
        // available - so a shortcut for the current layout simply does nothing.
        const bool forShortcut = m_shortcutProfile >= 0 && list->requestId == m_shortcutRefreshId;
        const int profile = m_shortcutProfile;
        if (forShortcut) {
            m_shortcutProfile = -1;
            m_shortcutRefreshId = 0;
        }
        discard();
        if (forShortcut) Activate(profile, m_owner);
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
    for (int i = 0; i < kProfileCount; ++i) {
        if (StateOf(i).active) {
            summary += L" ";
            summary += Strings::Format(Strings::Id::DisplaysSummaryActive, Strings::Get(kProfiles[i].title));
        }
    }
    return summary;
}

std::wstring DisplayWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0 || controlId >= kProfileCount) return {};
    const DisplayTopology::ProfileState state = StateOf(controlId);
    return Strings::Format(state.active ? Strings::Id::DisplaysRowActive : Strings::Id::DisplaysRow,
                           Strings::Get(kProfiles[controlId].title), Subtitle(controlId, state).c_str());
}

bool DisplayWidget::OnKeyDown(UINT key, int focusedControl) {
    switch (key) {
        case VK_UP:
            m_focusedControl = (focusedControl <= 0) ? kProfileCount - 1 : focusedControl - 1;
            break;
        case VK_DOWN:
            m_focusedControl = (focusedControl < 0 || focusedControl >= kProfileCount - 1)
                                   ? 0
                                   : focusedControl + 1;
            break;
        case VK_HOME:
            m_focusedControl = 0;
            break;
        case VK_END:
            m_focusedControl = kProfileCount - 1;
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

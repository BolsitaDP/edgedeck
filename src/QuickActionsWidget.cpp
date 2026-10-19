#include "QuickActionsWidget.h"
#include "ActionIcons.h"
#include "Actions.h"
#include "Config.h"
#include "Renderer.h"
#include "Strings.h"

#include <cwctype>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 44.0f;
// A row's picture, and the gap between it and the name. 24 is the size the shell's own lists
// draw these at; the lookup hands over a larger image, which is reduced to fit.
constexpr float kIconSize = 24.0f;
constexpr float kIconGap = 12.0f;
// A line of the panel's item text (13.5 px Segoe UI) is about this tall, and it is drawn from the
// top of the rectangle it is given: starting it this far below the row's top puts it level with
// the picture beside it.
constexpr float kLabelHeight = 18.0f;
// U+2261 IDENTICAL TO, written as a numeric code point (not a literal
// character in the source file) to avoid depending on the compiler's
// source-file encoding detection.
constexpr wchar_t kTabGlyph[] = {0x2261, 0};

// The letter a row without a picture shows: the first one in its name, upper-cased.
std::wstring BadgeLetter(const std::wstring& name) {
    for (wchar_t c : name) {
        if (std::iswalnum(c)) return std::wstring(1, static_cast<wchar_t>(std::towupper(c)));
    }
    return L"?";
}
} // namespace

QuickActionsWidget::QuickActionsWidget() : m_actions(Config::QuickActionList()) {
    ResetIcons({}, {});
}

void QuickActionsWidget::OnSettingsChanged() {
    std::vector<std::wstring> previousTargets = std::move(m_iconTargets);
    std::vector<Icon> previousIcons = std::move(m_icons);
    m_actions = Config::QuickActionList();
    // Whatever was hovered or focused may now be a different row, or none.
    m_hovered = -1;
    m_focused = -1;

    // A lookup still in flight was for the old list; its reply will find no request to answer.
    m_requests.Clear();
    ResetIcons(previousTargets, std::move(previousIcons));
    // The panel may be open right now (a pinned one); it needs the new rows' pictures without
    // waiting to be opened again. Before it has ever opened there is no window to answer to.
    if (m_owner) RequestIcons();
}

// Sizes the picture slots to the list. A row whose target is the same as one in the previous list
// keeps what was already learned about it, so editing a name or adding a row does not send every
// other row back to waiting.
void QuickActionsWidget::ResetIcons(const std::vector<std::wstring>& previousTargets,
                                    std::vector<Icon>&& previousIcons) {
    m_iconTargets.clear();
    m_icons.assign(m_actions.size(), Icon{});
    for (size_t i = 0; i < m_actions.size(); ++i) {
        m_iconTargets.push_back(Actions::ExpandVariables(m_actions[i].target));
        for (size_t j = 0; j < previousTargets.size() && j < previousIcons.size(); ++j) {
            if (previousTargets[j] == m_iconTargets[i] &&
                previousIcons[j].state == Icon::State::Ready) {
                m_icons[i] = previousIcons[j];
                break;
            }
        }
    }
}

void QuickActionsWidget::RequestIcons() {
    std::vector<std::wstring> wanted;
    for (size_t i = 0; i < m_icons.size(); ++i) {
        Icon& icon = m_icons[i];
        if (icon.state != Icon::State::Unknown) continue;

        if (m_actions[i].target == QuickActions::kShowDesktop) {
            icon.state = Icon::State::Ready; // nothing to look up; it takes the badge
            continue;
        }
        icon.state = Icon::State::Pending;
        bool already = false;
        for (const std::wstring& t : wanted) already = already || t == m_iconTargets[i];
        if (!already) wanted.push_back(m_iconTargets[i]);
    }
    if (wanted.empty() || !m_owner) return;

    ActionIcons::LoadAsync(std::move(wanted), m_owner, kIconsMessage, m_requests.Begin());
}

void QuickActionsWidget::OnPanelOpening(HWND ownerHwnd) {
    m_owner = ownerHwnd;
    RequestIcons();
}

void QuickActionsWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
    if (!envelope) return;

    // Anything not meant for this widget - a reply for a list that has since changed, or for a
    // widget Settings replaced - is dropped, and always freed here.
    if (message != kIconsMessage || envelope->Kind() != AsyncKind::ActionIcons) {
        delete envelope;
        return;
    }
    auto* batch = static_cast<ActionIcons::IconBatch*>(envelope);
    if (m_requests.Accept(batch->requestId)) {
        for (size_t i = 0; i < m_icons.size(); ++i) {
            if (m_icons[i].state != Icon::State::Pending) continue;
            for (size_t k = 0; k < batch->targets.size() && k < batch->images.size(); ++k) {
                if (batch->targets[k] != m_iconTargets[i]) continue;
                m_icons[i].state = Icon::State::Ready;
                m_icons[i].image = batch->images[k];
                break;
            }
        }
    }
    delete batch;
}

const wchar_t* QuickActionsWidget::TabGlyph() const {
    return kTabGlyph;
}

// U+E945, a lightning bolt. Numeric, like every glyph here, so the source stays ASCII.
const wchar_t* QuickActionsWidget::TabIcon() const {
    static constexpr wchar_t icon[] = {0xE945, 0};
    return icon;
}

const wchar_t* QuickActionsWidget::PanelTitle() const {
    return TR(QuickActionsTitle);
}

float QuickActionsWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    return kRowHeight * static_cast<float>(RowCount());
}

void QuickActionsWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    const PanelTheme& theme = PanelTheme::Current();
    for (int i = 0; i < RowCount(); ++i) {
        const size_t index = static_cast<size_t>(i);
        const float y = static_cast<float>(i) * kRowHeight;
        const D2D1_RECT_F rect = D2D1::RectF(kPaddingX, y, width - kPaddingX, y + kRowHeight);

        // The row's own highlight, with the name and the picture drawn over it.
        painter.DrawRow(rect, L"", i == m_hovered || i == m_focused);

        const float top = y + (kRowHeight - kIconSize) / 2.0f;
        const D2D1_RECT_F iconRect =
            D2D1::RectF(rect.left, top, rect.left + kIconSize, top + kIconSize);
        const Icon& icon = m_icons[index];
        if (icon.state == Icon::State::Ready) {
            if (icon.image) {
                painter.DrawImage(iconRect, *icon.image);
            } else {
                painter.DrawBadge(iconRect, BadgeLetter(m_actions[index].name).c_str(),
                                  theme.controlBg);
            }
        } // still being looked up: the slot stays empty for the instant it takes, not a flash of badge

        const float labelTop = y + (kRowHeight - kLabelHeight) / 2.0f;
        painter.DrawLabel(
            D2D1::RectF(iconRect.right + kIconGap, labelTop, rect.right, labelTop + kLabelHeight),
            m_actions[index].name.c_str(), false);
    }
}

int QuickActionsWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    if (x < 0.0f || x > width || y < 0.0f) return -1;
    int idx = static_cast<int>(y / kRowHeight);
    if (idx < 0 || idx >= RowCount()) return -1;
    return idx;
}

void QuickActionsWidget::Activate(int controlId, HWND ownerHwnd) {
    if (controlId < 0 || controlId >= RowCount()) return;

    std::wstring problem;
    if (!Actions::Run(m_actions[static_cast<size_t>(controlId)], problem)) {
        ReportProblem(ownerHwnd ? ownerHwnd : m_owner, problem);
    }
}

std::wstring QuickActionsWidget::AccessibleSummary() const { return TR(QuickActionsSummary); }

std::wstring QuickActionsWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0 || controlId >= RowCount()) return {};
    return m_actions[static_cast<size_t>(controlId)].name;
}

bool QuickActionsWidget::OnKeyDown(UINT key, int focusedControl) {
    const int count = RowCount();
    if (count == 0) return false;
    switch (key) {
        case VK_UP:
            m_focused = (focusedControl <= 0) ? count - 1 : focusedControl - 1;
            break;
        case VK_DOWN:
            m_focused = (focusedControl < 0 || focusedControl >= count - 1) ? 0 : focusedControl + 1;
            break;
        case VK_HOME:
            m_focused = 0;
            break;
        case VK_END:
            m_focused = count - 1;
            break;
        case VK_SPACE:
        case VK_RETURN:
            if (focusedControl >= 0) Activate(focusedControl, nullptr);
            return true;
        default:
            return false;
    }
    m_hovered = m_focused;
    return true;
}

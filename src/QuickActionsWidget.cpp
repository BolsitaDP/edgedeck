#include "QuickActionsWidget.h"
#include "Actions.h"
#include "Config.h"

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 44.0f;
// U+2261 IDENTICAL TO, written as a numeric code point (not a literal
// character in the source file) to avoid depending on the compiler's
// source-file encoding detection.
constexpr wchar_t kTabGlyph[] = {0x2261, 0};
} // namespace

QuickActionsWidget::QuickActionsWidget() : m_actions(Config::QuickActionList()) {}

void QuickActionsWidget::OnSettingsChanged() {
    m_actions = Config::QuickActionList();
    // Whatever was hovered or focused may now be a different row, or none.
    m_hovered = -1;
    m_focused = -1;
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
    return L"Quick Actions";
}

float QuickActionsWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    return kRowHeight * static_cast<float>(RowCount());
}

void QuickActionsWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    for (int i = 0; i < RowCount(); ++i) {
        float y = static_cast<float>(i) * kRowHeight;
        D2D1_RECT_F rect = D2D1::RectF(kPaddingX, y, width - kPaddingX, y + kRowHeight);
        painter.DrawRow(rect, m_actions[static_cast<size_t>(i)].name.c_str(),
                        i == m_hovered || i == m_focused);
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

std::wstring QuickActionsWidget::AccessibleSummary() const { return L"Quick Actions."; }

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

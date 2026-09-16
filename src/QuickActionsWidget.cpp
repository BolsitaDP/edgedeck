#include "QuickActionsWidget.h"
#include "Actions.h"

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 44.0f;
constexpr const wchar_t* kLabels[] = {L"Open Notepad", L"Open Calculator", L"Show Desktop"};
constexpr int kRowCount = 3;
// Written as a numeric code point (not a literal character in the source
// file) to avoid depending on the compiler's source-file encoding detection.
constexpr wchar_t kTabGlyph[] = {0x2261, 0}; // U+2261 IDENTICAL TO
} // namespace

const wchar_t* QuickActionsWidget::TabGlyph() const {
    return kTabGlyph;
}

const wchar_t* QuickActionsWidget::PanelTitle() const {
    return L"Quick Actions";
}

float QuickActionsWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    return kRowHeight * kRowCount;
}

void QuickActionsWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    for (int i = 0; i < kRowCount; ++i) {
        float y = static_cast<float>(i) * kRowHeight;
        D2D1_RECT_F rect = D2D1::RectF(kPaddingX, y, width - kPaddingX, y + kRowHeight);
        painter.DrawRow(rect, kLabels[i], i == m_hovered);
    }
}

int QuickActionsWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    if (x < 0.0f || x > width) return -1;
    int idx = static_cast<int>(y / kRowHeight);
    if (idx < 0 || idx >= kRowCount) return -1;
    return idx;
}

void QuickActionsWidget::Activate(int controlId, HWND /*ownerHwnd*/) {
    bool succeeded = true;
    switch (controlId) {
        case 0: succeeded = Actions::OpenNotepad(); break;
        case 1: succeeded = Actions::OpenCalculator(); break;
        case 2: succeeded = Actions::ShowDesktopToggle(); break;
        default: return;
    }
    if (!succeeded) {
        MessageBoxW(nullptr, L"Windows could not complete this action.", L"EdgeDeck",
                    MB_ICONERROR | MB_OK | MB_TOPMOST);
    }
}

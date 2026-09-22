#include "BrightnessWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 54.0f;
constexpr float kRowGap = 4.0f;
constexpr float kNameHeight = 24.0f;
constexpr float kValueWidth = 44.0f;
constexpr float kGrabSlack = 8.0f; // extra horizontal grab area around the track ends

// Numeric code point, not a literal character - see QuickActionsWidget.cpp.
constexpr wchar_t kTabGlyph[] = {0x263C, 0}; // U+263C WHITE SUN WITH RAYS

float RowTop(int row) { return static_cast<float>(row) * (kRowHeight + kRowGap); }

D2D1_RECT_F TrackRect(int row, float width) {
    float top = RowTop(row) + kNameHeight;
    return D2D1::RectF(kPaddingX, top, width - kPaddingX - kValueWidth, RowTop(row) + kRowHeight);
}
} // namespace

const wchar_t* BrightnessWidget::TabGlyph() const { return kTabGlyph; }
const wchar_t* BrightnessWidget::PanelTitle() const { return L"Brightness"; }

float BrightnessWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_rows.empty()) return 40.0f;
    return static_cast<float>(m_rows.size()) * kRowHeight +
           static_cast<float>(m_rows.size() - 1) * kRowGap;
}

void BrightnessWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_rows.empty()) {
        D2D1_RECT_F rect = D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, 40.0f);
        painter.DrawLabel(rect, m_hasLoaded ? L"No monitors found" : L"Checking displays...", true);
        return;
    }

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& r = m_rows[i];
        float top = RowTop(i);
        painter.DrawLabel(D2D1::RectF(kPaddingX, top, width - kPaddingX, top + kNameHeight),
                           r.info.name.c_str(), false);

        D2D1_RECT_F track = TrackRect(i, width);
        if (!r.info.supported) {
            painter.DrawLabel(D2D1::RectF(kPaddingX, track.top, width - kPaddingX, track.bottom),
                               L"Brightness control not available", true);
            continue;
        }

        bool active = (m_hoveredRow == i) || (m_dragRow == i);
        painter.DrawSlider(track, static_cast<float>(r.info.percent) / 100.0f, active, true);

        wchar_t text[16];
        swprintf_s(text, L"%d%%", r.info.percent);
        float cy = (track.top + track.bottom) / 2.0f;
        painter.DrawValueText(D2D1::RectF(track.right + 4.0f, cy - 11.0f, width - kPaddingX, cy + 11.0f),
                               text, false);
    }
}

int BrightnessWidget::RowAtY(float y) const {
    if (m_rows.empty() || y < 0.0f) return -1;
    int row = static_cast<int>(y / (kRowHeight + kRowGap));
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return -1;
    // Only the slider band (below the name line) counts, not the gap between rows.
    float local = y - RowTop(row);
    if (local < kNameHeight - 2.0f || local > kRowHeight) return -1;
    return row;
}

int BrightnessWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    int row = RowAtY(y);
    if (row < 0 || !m_rows[row].info.supported) return -1;
    D2D1_RECT_F track = TrackRect(row, width);
    if (x < track.left - kGrabSlack || x > track.right + kGrabSlack) return -1;
    return row;
}

void BrightnessWidget::ApplyPointer(int row, float x, float width) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    D2D1_RECT_F track = TrackRect(row, width);
    float span = track.right - track.left;
    if (span <= 0.0f) return;

    float value01 = std::clamp((x - track.left) / span, 0.0f, 1.0f);
    int percent = static_cast<int>(std::lround(value01 * 100.0f));
    if (percent == m_rows[row].info.percent) return;

    // Only the on-screen value follows the pointer. The DDC/CI write happens
    // once, when the button is released (OnDragEnd) - many monitors persist
    // the brightness in non-volatile memory, so streaming writes mid-drag
    // would just wear it for no benefit.
    m_rows[row].info.percent = percent;
}

void BrightnessWidget::Pump(int row) {
    Row& r = m_rows[row];
    if (r.inFlight || !r.handle || !m_owner) return;
    if (r.info.percent == r.sentPercent) return;

    r.inFlight = true;
    r.sentPercent = r.info.percent;
    BrightnessControls::SetBrightness(r.handle, r.info.percent, row, m_owner, kSetResultMessage);
}

bool BrightnessWidget::OnDragBegin(float x, float y, float width, float /*contentHeight*/,
                                    HWND ownerHwnd) {
    m_owner = ownerHwnd;
    int row = RowAtY(y);
    if (row < 0 || !m_rows[row].info.supported) return false;

    D2D1_RECT_F track = TrackRect(row, width);
    if (x < track.left - kGrabSlack || x > track.right + kGrabSlack) return false;

    m_dragRow = row;
    ApplyPointer(row, x, width);
    return true;
}

void BrightnessWidget::OnDragMove(float x, float /*y*/, float width, float /*contentHeight*/) {
    ApplyPointer(m_dragRow, x, width);
}

void BrightnessWidget::OnDragEnd() {
    if (m_dragRow >= 0) Pump(m_dragRow);
    m_dragRow = -1;
}

void BrightnessWidget::OnPanelOpening(HWND ownerHwnd) {
    m_owner = ownerHwnd;
    BrightnessControls::RefreshMonitors(ownerHwnd, kRefreshMessage);
}

void BrightnessWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    if (message == kRefreshMessage) {
        auto* list = reinterpret_cast<BrightnessControls::MonitorList*>(wParam);
        if (!list) return;

        // A stale refresh must not replace rows out from under an active drag.
        if (m_dragRow >= 0) {
            delete list;
            return;
        }

        m_rows.clear();
        for (size_t i = 0; i < list->infos.size(); ++i) {
            Row r;
            r.info = list->infos[i];
            r.handle = list->handles[i];
            r.sentPercent = r.info.percent;
            m_rows.push_back(std::move(r));
        }
        delete list;
        m_hasLoaded = true;
        m_hoveredRow = -1;
        return;
    }

    if (message == kSetResultMessage) {
        int row = 0;
        bool succeeded = false;
        BrightnessControls::UnpackResult(wParam, row, succeeded);
        if (row >= 0 && row < static_cast<int>(m_rows.size())) {
            m_rows[row].inFlight = false;
            // A new drag may have started while the previous write was in
            // flight; that one is sent on release, not mid-drag.
            if (m_dragRow != row) Pump(row);
        }
    }
}

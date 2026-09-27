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
constexpr int kStepPercent = 5;    // keyboard adjustment granularity

float RowTop(int row) { return static_cast<float>(row) * (kRowHeight + kRowGap); }

D2D1_RECT_F TrackRect(int row, float width) {
    float top = RowTop(row) + kNameHeight;
    return D2D1::RectF(kPaddingX, top, width - kPaddingX - kValueWidth, RowTop(row) + kRowHeight);
}
} // namespace

const wchar_t* BrightnessWidget::TabGlyph() const { return L"\x263C"; } // ☀
const wchar_t* BrightnessWidget::PanelTitle() const { return L"Brightness"; }

float BrightnessWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_rows.empty()) return 40.0f;
    return static_cast<float>(m_rows.size()) * kRowHeight +
           static_cast<float>(m_rows.size() - 1) * kRowGap;
}

void BrightnessWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_rows.empty()) {
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, 40.0f),
                          m_hasLoaded ? L"No monitors found" : L"Checking displays...", true);
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

        bool active = (m_hoveredControl == i) || (m_dragRow == i) || (m_focusedControl == i);
        bool pending = r.writeRequestId != 0;
        painter.DrawSlider(track, static_cast<float>(r.info.percent) / 100.0f, 0.0f, 1.0f, active,
                           !pending);

        wchar_t text[24];
        // While a write is in flight the value is provisional, so it is drawn
        // muted: the number only reaches full contrast once the monitor has
        // confirmed it. That plus snapping back on a rejected write is what
        // stops the slider from claiming a brightness the panel is not at.
        swprintf_s(text, r.writeFailed ? L"%d%% !" : L"%d%%", r.info.percent);
        float cy = (track.top + track.bottom) / 2.0f;
        painter.DrawValueText(
            D2D1::RectF(track.right + 4.0f, cy - 11.0f, width - kPaddingX, cy + 11.0f), text,
            pending || r.writeFailed);
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

bool BrightnessWidget::ApplyPointer(int row, float x, float width) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return false;
    D2D1_RECT_F track = TrackRect(row, width);
    float span = track.right - track.left;
    if (span <= 0.0f) return false;

    float value01 = std::clamp((x - track.left) / span, 0.0f, 1.0f);
    int percent = static_cast<int>(std::lround(value01 * 100.0f));
    if (percent == m_rows[row].info.percent) return false;

    // Only the on-screen value follows the pointer. The DDC/CI write happens
    // once, when the button is released (OnDragEnd) - many monitors persist the
    // brightness in non-volatile memory, so streaming writes mid-drag would
    // just wear it for no benefit.
    m_rows[row].info.percent = percent;
    return true;
}

void BrightnessWidget::Send(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    Row& r = m_rows[row];
    if (!r.info.supported || !r.handle) return;
    if (r.writeRequestId != 0) return; // already writing; the reply re-sends if needed
    if (r.info.percent == r.sentPercent) return;

    r.writeRequestId = m_requests.Begin();
    r.sentPercent = r.info.percent;
    BrightnessControls::SetBrightness(r.handle, r.info.percent, row, r.info.devicePath,
                                      r.writeRequestId, m_listGeneration, m_owner,
                                      kSetResultMessage);
}

void BrightnessWidget::SnapTo(int row, int percent) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    Row& r = m_rows[row];
    percent = std::clamp(percent, 0, 100);

    if (r.writeRequestId != 0) {
        // A write is already in flight for this monitor. Remember where the
        // user actually wants it and let the reply chain another send, rather
        // than dropping the new value on the floor.
        r.info.percent = percent;
        return;
    }

    r.info.percent = percent;
    Send(row);
}

bool BrightnessWidget::OnDragBegin(float x, float y, float width, float /*contentHeight*/,
                                   HWND ownerHwnd) {
    m_owner = ownerHwnd;
    const int row = RowAtY(y);
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
    if (m_dragRow >= 0) Send(m_dragRow);
    m_dragRow = -1;
}

void BrightnessWidget::OnPanelOpening(HWND ownerHwnd) {
    m_owner = ownerHwnd;
    if (m_refreshRequestId != 0) return;
    m_listGeneration++;
    m_refreshRequestId = m_requests.Begin();
    BrightnessControls::RefreshMonitors(ownerHwnd, kRefreshMessage, m_refreshRequestId,
                                         m_listGeneration);
}

void BrightnessWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
    if (!envelope) return;
    auto discard = [&] { delete envelope; };

    if (message == kRefreshMessage) {
        auto* list = static_cast<BrightnessControls::MonitorList*>(envelope);
        if (!m_requests.Accept(list->requestId) || list->listGeneration != m_listGeneration) {
            return discard();
        }
        m_refreshRequestId = 0;

        // A refresh must not replace rows out from under an active drag.
        if (m_dragRow >= 0) return discard();

        m_rows.clear();
        for (size_t i = 0; i < list->infos.size(); ++i) {
            Row r;
            r.info = list->infos[i];
            r.handle = list->handles[i];
            r.sentPercent = r.info.percent;
            r.confirmedPercent = r.info.percent;
            m_rows.push_back(std::move(r));
        }
        m_hasLoaded = true;
        m_hoveredControl = -1;
        if (m_focusedControl >= static_cast<int>(m_rows.size())) m_focusedControl = -1;
        return discard();
    }

    if (message == kSetResultMessage) {
        auto* result = static_cast<BrightnessControls::SetResult*>(envelope);
        int row = result->row;
        bool sameList = result->listGeneration == m_listGeneration;
        if (sameList && row >= 0 && row < static_cast<int>(m_rows.size()) &&
            m_rows[row].writeRequestId == result->requestId) {
            Row& r = m_rows[row];
            r.writeRequestId = 0;
            m_requests.Retire(result->requestId);

            if (result->succeeded) {
                r.confirmedPercent = result->percent;
                r.info.percent = result->percent;
                r.sentPercent = result->percent;
                r.writeFailed = false;
            } else {
                // The monitor rejected the write. Put the row back to the last
                // value the hardware actually confirmed, so the slider stops
                // claiming a brightness the panel is not at. The old code
                // simply cleared the in-flight flag and left the optimistic
                // value on screen forever, with no retry and no indication.
                r.info.percent = r.confirmedPercent;
                r.sentPercent = r.confirmedPercent;
                r.writeFailed = true;
            }

            // The user may have moved the slider again while this was in
            // flight; that newer value is still pending, so send it now.
            if (r.info.percent != r.sentPercent) Send(row);
            return discard();
        }
        return discard();
    }

    discard();
}

std::wstring BrightnessWidget::AccessibleSummary() const {
    if (m_rows.empty()) return L"Brightness. No monitors found.";
    std::wstring out = L"Brightness. ";
    out += std::to_wstring(m_rows.size());
    out += (m_rows.size() == 1) ? L" monitor." : L" monitors.";
    return out;
}

std::wstring BrightnessWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0 || controlId >= static_cast<int>(m_rows.size())) return {};
    const Row& r = m_rows[controlId];
    if (!r.info.supported) return r.info.name + L": brightness control not available.";
    return r.info.name + L": brightness " + std::to_wstring(r.info.percent) + L" percent.";
}

bool BrightnessWidget::OnKeyDown(UINT key, int focusedControl) {
    int total = static_cast<int>(m_rows.size());
    if (total <= 0) return false;

    // Left/right are handled by the caller (Tab) via OnStepControl, because
    // they change a value rather than move the selection.
    int next = focusedControl;
    switch (key) {
        case VK_UP:
        case VK_DOWN: {
            int delta = (key == VK_UP) ? -1 : 1;
            int newRow = focusedControl + delta;
            if (newRow < 0) newRow = total - 1;
            if (newRow >= total) newRow = 0;
            next = newRow;
            break;
        }
        case VK_HOME:
            next = 0;
            break;
        case VK_END:
            next = total - 1;
            break;
        default:
            return false;
    }

    m_focusedControl = next;
    m_hoveredControl = next;
    return true;
}

bool BrightnessWidget::OnStepControl(int controlId, int direction, float& outValue01) {
    if (controlId < 0 || controlId >= static_cast<int>(m_rows.size())) return false;
    if (!m_rows[controlId].info.supported) return false;

    int current = m_rows[controlId].info.percent;
    int updated = std::clamp(current + (direction > 0 ? kStepPercent : -kStepPercent), 0, 100);
    if (updated == current) return false;

    SnapTo(controlId, updated);
    outValue01 = static_cast<float>(updated) / 100.0f;
    return true;
}

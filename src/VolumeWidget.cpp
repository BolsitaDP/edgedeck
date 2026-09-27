#include "VolumeWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 54.0f;
constexpr float kRowGap = 4.0f;
constexpr float kNameHeight = 24.0f;
constexpr float kValueWidth = 44.0f;
constexpr float kMuteSize = 22.0f;
constexpr float kMuteGap = 8.0f;
constexpr float kGrabSlack = 8.0f;
constexpr int kStepPercent = 5;
} // namespace

const wchar_t* VolumeWidget::TabGlyph() const { return L"\x266A\xFE0E"; } // ♪︎
const wchar_t* VolumeWidget::PanelTitle() const { return L"Volume"; }

float VolumeWidget::RowTop(int row) const {
    return static_cast<float>(row) * (kRowHeight + kRowGap);
}

float VolumeWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_rows.empty()) return 40.0f;
    return static_cast<float>(m_rows.size()) * kRowHeight +
           static_cast<float>(m_rows.size() - 1) * kRowGap;
}

namespace {
D2D1_RECT_F TrackRectFor(int row, float width) {
    float top = row * (kRowHeight + kRowGap) + kNameHeight;
    return D2D1::RectF(kPaddingX, top, width - kPaddingX - kValueWidth - kMuteSize - kMuteGap,
                       row * (kRowHeight + kRowGap) + kRowHeight);
}

D2D1_RECT_F MuteRectFor(int row, float width) {
    D2D1_RECT_F track = TrackRectFor(row, width);
    float x = width - kPaddingX - kMuteSize;
    float cy = (track.top + track.bottom) / 2.0f;
    return D2D1::RectF(x, cy - kMuteSize / 2.0f, x + kMuteSize, cy + kMuteSize / 2.0f);
}
} // namespace

void VolumeWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_rows.empty()) {
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, 40.0f),
                          m_hasLoaded ? L"No audio output devices" : L"Checking audio...", true);
        return;
    }

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& r = m_rows[i];
        float top = RowTop(i);

        std::wstring name = r.info.name;
        if (r.info.isDefault) name += L"  (default)";
        painter.DrawLabel(D2D1::RectF(kPaddingX, top, width - kPaddingX, top + kNameHeight),
                          name.c_str(), false);

        D2D1_RECT_F track = TrackRectFor(i, width);
        bool active = (m_hovered == i * 2) || (m_dragRow == i) || (m_focused == i * 2);
        // A muted device's slider still shows its level - greying it out and
        // drawing it empty would conflate "muted" with "at zero".
        painter.DrawSlider(track, static_cast<float>(r.info.percent) / 100.0f, 0.0f, 1.0f, active,
                           !r.info.muted);

        wchar_t text[16];
        swprintf_s(text, L"%d%%", r.info.percent);
        painter.DrawValueText(
            D2D1::RectF(track.right + 4.0f, (track.top + track.bottom) / 2.0f - 11.0f,
                        track.right + 4.0f + kValueWidth, (track.top + track.bottom) / 2.0f + 11.0f),
            text, r.info.muted);

        int muteId = i * 2 + kMuteButton;
        painter.DrawMuteButton(MuteRectFor(i, width), r.info.muted, m_hovered == muteId);
    }
}

int VolumeWidget::RowAtY(float y) const {
    if (m_rows.empty() || y < 0.0f) return -1;
    int row = static_cast<int>(y / (kRowHeight + kRowGap));
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return -1;
    float local = y - RowTop(row);
    if (local < kNameHeight - 2.0f || local > kRowHeight) return -1;
    return row;
}

int VolumeWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    int row = RowAtY(y);
    if (row < 0) return -1;

    D2D1_RECT_F mute = MuteRectFor(row, width);
    if (x >= mute.left && x <= mute.right && y >= mute.top && y <= mute.bottom) {
        return row * 2 + kMuteButton;
    }

    D2D1_RECT_F track = TrackRectFor(row, width);
    if (x >= track.left - kGrabSlack && x <= track.right + kGrabSlack) return row * 2;
    return -1;
}

bool VolumeWidget::Commit(int row, int percent, bool muted) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return false;
    Row& r = m_rows[row];

    if (!VolumeControls::SetVolume(r.handle, percent, muted)) {
        // The endpoint refused. Re-read rather than leave the panel claiming a
        // level the system is not using.
        VolumeControls::DeviceInfo actual;
        if (VolumeControls::QueryState(r.handle, actual)) r.info = actual;
        return false;
    }
    r.info.percent = std::clamp(percent, 0, 100);
    r.info.muted = muted;
    return true;
}

void VolumeWidget::Activate(int controlId, HWND /*ownerHwnd*/) {
    if (controlId < 0) return;
    int row = controlId / 2;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    if (controlId % 2 == kMuteButton) {
        Commit(row, m_rows[row].info.percent, !m_rows[row].info.muted);
    }
}

void VolumeWidget::Refresh() {
    std::vector<VolumeControls::DeviceInfo> infos;
    std::vector<std::shared_ptr<VolumeControls::DeviceHandle>> handles;
    VolumeControls::RefreshDevices(infos, handles);

    // Keep the drag going on the same device if it survived the re-read.
    std::shared_ptr<VolumeControls::DeviceHandle> dragging;
    if (m_dragRow >= 0 && m_dragRow < static_cast<int>(m_rows.size())) {
        dragging = m_rows[m_dragRow].handle;
    }

    m_rows.clear();
    for (size_t i = 0; i < infos.size(); ++i) {
        Row r;
        r.info = infos[i];
        r.handle = handles[i];
        m_rows.push_back(std::move(r));
    }
    m_hasLoaded = true;

    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (dragging && m_rows[i].handle == dragging) {
            m_dragRow = static_cast<int>(i);
            break;
        }
    }
}

void VolumeWidget::OnPanelOpening(HWND /*ownerHwnd*/) { Refresh(); }

void VolumeWidget::OnPanelVisibilityChanged(bool visible) {
    if (visible) Refresh();
}

void VolumeWidget::OnTick() {
    // Only re-reads the state of endpoints that are already open. Re-enumerating
    // the whole device collection twice a second would be several dozen COM
    // round trips per tick for no benefit: devices appearing or disappearing is
    // handled on panel open, and the per-device values are what change. This
    // also keeps the panel in step with volume keys pressed in another app.
    for (Row& r : m_rows) {
        VolumeControls::DeviceInfo updated;
        if (VolumeControls::QueryState(r.handle, updated)) {
            updated.isDefault = r.info.isDefault;
            r.info = std::move(updated);
        }
    }
}

bool VolumeWidget::OnDragBegin(float x, float y, float width, float /*contentHeight*/,
                               HWND /*ownerHwnd*/) {
    int row = RowAtY(y);
    if (row < 0) return false;
    if (HitTest(x, y, width, 0) != row * 2) return false; // the mute button claims its own press

    m_dragRow = row;
    D2D1_RECT_F track = TrackRectFor(row, width);
    float span = track.right - track.left;
    if (span > 0.0f) {
        float value01 = std::clamp((x - track.left) / span, 0.0f, 1.0f);
        // Dragging a muted slider up is an unmute: that is what the gesture
        // obviously means, and it matches what every other mixer does.
        Commit(row, static_cast<int>(std::lround(value01 * 100.0f)), false);
    }
    return true;
}

void VolumeWidget::OnDragMove(float x, float /*y*/, float width, float /*contentHeight*/) {
    if (m_dragRow < 0) return;
    D2D1_RECT_F track = TrackRectFor(m_dragRow, width);
    float span = track.right - track.left;
    if (span <= 0.0f) return;
    float value01 = std::clamp((x - track.left) / span, 0.0f, 1.0f);
    Commit(m_dragRow, static_cast<int>(std::lround(value01 * 100.0f)), false);
}

void VolumeWidget::OnDragEnd() { m_dragRow = -1; }

std::wstring VolumeWidget::AccessibleSummary() const {
    if (m_rows.empty()) return L"Volume. No audio output devices.";
    return L"Volume. " + std::to_wstring(m_rows.size()) +
           (m_rows.size() == 1 ? L" output device." : L" output devices.");
}

std::wstring VolumeWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0) return {};
    int row = controlId / 2;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return {};

    const Row& r = m_rows[row];
    if (controlId % 2 == kMuteButton) {
        return r.info.name + (r.info.muted ? L": muted. Activate to unmute."
                                           : L": unmuted. Activate to mute.");
    }
    return r.info.name + L": volume " + std::to_wstring(r.info.percent) + L" percent.";
}

bool VolumeWidget::OnKeyDown(UINT key, int focusedControl) {
    int total = static_cast<int>(m_rows.size()) * 2;
    if (total <= 0) return false;

    int next = focusedControl;
    switch (key) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN: {
            int delta = (key == VK_LEFT || key == VK_UP) ? -1 : 1;
            int newRow = focusedControl / 2 + delta;
            if (newRow < 0) newRow = static_cast<int>(m_rows.size()) - 1;
            if (newRow >= static_cast<int>(m_rows.size())) newRow = 0;
            next = newRow * 2 + (focusedControl % 2);
            break;
        }
        case VK_SPACE:
        case VK_RETURN:
            if (focusedControl >= 0) Activate(focusedControl, nullptr);
            return true;
        default:
            return false;
    }

    m_focused = next;
    m_hovered = next;
    return true;
}

bool VolumeWidget::OnStepControl(int controlId, int direction, float& outValue01) {
    if (controlId < 0 || controlId >= static_cast<int>(m_rows.size()) * 2) return false;
    if (controlId % 2 != 0) return false; // the mute toggle is not a slider

    int row = controlId / 2;
    int updated = std::clamp(m_rows[row].info.percent + (direction > 0 ? kStepPercent : -kStepPercent),
                             0, 100);
    if (!Commit(row, updated, m_rows[row].info.muted)) return false;
    outValue01 = static_cast<float>(updated) / 100.0f;
    return true;
}

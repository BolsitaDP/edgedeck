#include "Diagnostics.h"
#include "Strings.h"
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
constexpr float kTagGap = 8.0f;
constexpr float kTextLineHeight = 18.0f; // one line of the 13.5 px item text
constexpr float kUseWidth = 52.0f;
constexpr float kUseHeight = 20.0f;
constexpr float kMicGap = 10.0f;     // between the outputs and the microphone
constexpr float kMicRowHeight = 30.0f;
constexpr int kStepPercent = 5;
} // namespace

// U+25C4, deliberately not a note: with Media's character, two tabs in the bar were
// identical whenever the icon font is missing. This is only that fallback.
const wchar_t* VolumeWidget::TabGlyph() const { return L"%C4"; }
// U+E767, a speaker with sound waves.
const wchar_t* VolumeWidget::TabIcon() const {
    static constexpr wchar_t icon[] = {0xE767, 0};
    return icon;
}

const wchar_t* VolumeWidget::PanelTitle() const { return TR(VolumeTitle); }

float VolumeWidget::RowTop(int row) const {
    return static_cast<float>(row) * (kRowHeight + kRowGap);
}

float VolumeWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_rows.empty()) return 40.0f;
    return MicTop() + (m_hasMic ? kMicRowHeight : 0.0f);
}

float VolumeWidget::MicTop() const {
    if (m_rows.empty()) return 0.0f;
    return static_cast<float>(m_rows.size()) * kRowHeight + static_cast<float>(m_rows.size() - 1) * kRowGap + kMicGap;
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

// The "Use" button sits where the default output has its tag: at the end of the name's line.
D2D1_RECT_F UseRectFor(int row, float width) {
    const float top = row * (kRowHeight + kRowGap) + (kTextLineHeight - kUseHeight) / 2.0f + 1.0f;
    return D2D1::RectF(width - kPaddingX - kUseWidth, top, width - kPaddingX, top + kUseHeight);
}

D2D1_RECT_F MicButtonRect(float micTop, float width) {
    const float cy = micTop + kMicRowHeight / 2.0f;
    return D2D1::RectF(width - kPaddingX - kMuteSize, cy - kMuteSize / 2.0f, width - kPaddingX, cy + kMuteSize / 2.0f);
}
} // namespace

void VolumeWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_rows.empty()) {
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, 40.0f),
                          m_hasLoaded ? TR(VolumeNoDevices) : TR(VolumeChecking), true);
        return;
    }

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& r = m_rows[i];
        float top = RowTop(i);

        // The default device says so in a tag at the end of its line, and its name is trimmed
        // before the tag: appended to the name, the mark was the first thing a long name lost.
        D2D1_RECT_F nameRect = D2D1::RectF(kPaddingX, top, width - kPaddingX, top + kNameHeight);
        if (r.info.isDefault) {
            // Centred on the name's line of text, which sits at the top of its rect.
            const D2D1_RECT_F line = D2D1::RectF(nameRect.left, top, nameRect.right, top + kTextLineHeight);
            nameRect.right = painter.DrawTag(line, TR(VolumeDefault)) - kTagGap;
        } else {
            const D2D1_RECT_F use = UseRectFor(i, width);
            painter.DrawTextButton(use, TR(VolumeUse), m_hovered == kUseControl + i);
            nameRect.right = use.left - kTagGap;
        }
        painter.DrawLabel(nameRect, r.info.name.c_str(), false);

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

    if (m_hasMic) {
        const float micTop = MicTop();
        const D2D1_RECT_F button = MicButtonRect(micTop, width);
        const float textTop = micTop + (kMicRowHeight - kTextLineHeight) / 2.0f - 1.0f;
        // The device's name alone: Windows names recording devices "Microphone (...)" already, and
        // the button beside it is a microphone.
        painter.DrawLabel(D2D1::RectF(kPaddingX, textTop, button.left - kTagGap, textTop + kNameHeight),
                          m_mic.name.c_str(), m_mic.muted);
        const int micId = MicControl();
        painter.DrawMicButton(button, m_mic.muted, m_hovered == micId || m_focused == micId);
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
    auto inside = [&](const D2D1_RECT_F& r) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
    if (m_hasMic && inside(MicButtonRect(MicTop(), width))) return MicControl();
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (!m_rows[i].info.isDefault && inside(UseRectFor(i, width))) return kUseControl + i;
    }

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

    if (!VolumeControls::SetVolume(r.info.id, percent, muted)) {
        // The endpoint refused. Re-read rather than leave the panel claiming a
        // level the system is not using.
        VolumeControls::DeviceInfo actual;
        actual.id = r.info.id;
        if (VolumeControls::QueryState(r.info.id, actual)) {
            // Keep the name and default flag; only the live state is replaced.
            const std::wstring name = r.info.name;
            const bool isDefault = r.info.isDefault;
            r.info = actual;
            r.info.name = name;
            r.info.isDefault = isDefault;
        }
        return false;
    }
    r.info.percent = std::clamp(percent, 0, 100);
    r.info.muted = muted;
    return true;
}

void VolumeWidget::ToggleMicrophone() {
    if (!m_hasMic) return;
    if (VolumeControls::SetMicrophoneMuted(m_mic.id, !m_mic.muted)) {
        m_mic.muted = !m_mic.muted;
    } else {
        // Refused: show what is really so.
        VolumeControls::MicrophoneInfo actual;
        m_hasMic = VolumeControls::QueryMicrophone(actual);
        if (m_hasMic) m_mic = actual;
    }
}

void VolumeWidget::Activate(int controlId, HWND /*ownerHwnd*/) {
    if (controlId < 0) return;
    if (m_hasMic && controlId == MicControl()) return ToggleMicrophone();
    if (controlId >= kUseControl) {
        const int row = controlId - kUseControl;
        if (row >= static_cast<int>(m_rows.size())) return;
        // Re-listed either way: the default device is listed first, and its tag moves.
        VolumeControls::SetDefaultOutput(m_rows[row].info.id);
        Refresh();
        return;
    }
    int row = controlId / 2;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    if (controlId % 2 == kMuteButton) {
        Commit(row, m_rows[row].info.percent, !m_rows[row].info.muted);
    }
}

void VolumeWidget::Refresh() {
    std::vector<VolumeControls::DeviceInfo> infos;
    VolumeControls::RefreshDevices(infos);

    // Keep the drag going on the same device if it survived the re-read. Rows
    // are identified by endpoint id, which is stable for a given device.
    std::wstring dragging;
    if (m_dragRow >= 0 && m_dragRow < static_cast<int>(m_rows.size())) {
        dragging = m_rows[m_dragRow].info.id;
    }

    m_rows.clear();
    for (const auto& info : infos) {
        Row r;
        r.info = info;
        m_rows.push_back(std::move(r));
    }
    m_hasLoaded = true;
    VolumeControls::MicrophoneInfo mic;
    m_hasMic = VolumeControls::QueryMicrophone(mic);
    m_mic = m_hasMic ? mic : VolumeControls::MicrophoneInfo{};

    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (!dragging.empty() && m_rows[i].info.id == dragging) {
            m_dragRow = static_cast<int>(i);
            break;
        }
    }
}

void VolumeWidget::OnPanelOpening(HWND /*ownerHwnd*/) { Refresh(); }

void VolumeWidget::OnPanelVisibilityChanged(bool) {
    // Deliberately does nothing. OnPanelOpening already listed the endpoints a
    // fraction of a second before the slide finished, and listing them again at
    // that point costs a full enumeration to learn nothing new. Endpoints
    // appearing or disappearing is a rare event next to a slide; OnTick is what
    // keeps the levels honest in the meantime.
}

void VolumeWidget::OnTick() {
    // One enumeration for the whole panel, not one per row. Querying each device
    // separately re-walks the endpoint collection every time, which turns a poll
    // that should be nearly free into dozens of COM round trips twice a second -
    // measured at ~27ms per tick against ~4ms for the batched form on a
    // seven-device machine.
    //
    // Devices appearing or disappearing is still handled on panel open; what
    // changes between polls is the levels, and that is what this reads. It also
    // keeps the panel in step with volume keys pressed in another app.
    if (m_rows.empty()) return;

    std::vector<VolumeControls::DeviceInfo> live;
    live.reserve(m_rows.size());
    for (const Row& r : m_rows) live.push_back(r.info);

    if (!VolumeControls::RefreshStates(live)) return;

    for (size_t i = 0; i < m_rows.size() && i < live.size(); ++i) {
        m_rows[i].info.percent = live[i].percent;
        m_rows[i].info.muted = live[i].muted;
    }

    // The microphone too: muted from a headset's button or a call app, it says so here.
    if (m_hasMic) {
        VolumeControls::MicrophoneInfo mic;
        if (VolumeControls::QueryMicrophone(mic) && mic.id == m_mic.id) m_mic.muted = mic.muted;
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
    if (m_rows.empty()) return TR(VolumeSummaryNone);
    return Strings::Format(m_rows.size() == 1 ? Strings::Id::VolumeSummaryOne : Strings::Id::VolumeSummaryMany,
                           m_rows.size());
}

std::wstring VolumeWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0) return {};
    if (m_hasMic && controlId == MicControl()) {
        return Strings::Format(m_mic.muted ? Strings::Id::VolumeMicMuted : Strings::Id::VolumeMicOn, m_mic.name.c_str());
    }
    if (controlId >= kUseControl) {
        const int row = controlId - kUseControl;
        if (row >= static_cast<int>(m_rows.size())) return {};
        return Strings::Format(Strings::Id::VolumeUseNamed, m_rows[row].info.name.c_str());
    }
    int row = controlId / 2;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return {};

    const Row& r = m_rows[row];
    if (controlId % 2 == kMuteButton) {
        return Strings::Format(r.info.muted ? Strings::Id::VolumeMuted : Strings::Id::VolumeUnmuted,
                               r.info.name.c_str());
    }
    return Strings::Format(Strings::Id::VolumeLevel, r.info.name.c_str(), static_cast<int>(r.info.percent));
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
            // The microphone is one more row below the outputs, with a single control.
            const int rowCount = static_cast<int>(m_rows.size()) + (m_hasMic ? 1 : 0);
            const bool onMic = m_hasMic && focusedControl == MicControl();
            int delta = (key == VK_LEFT || key == VK_UP) ? -1 : 1;
            int newRow = (focusedControl < 0 ? -1 : focusedControl / 2) + delta;
            if (newRow < 0) newRow = rowCount - 1;
            if (newRow >= rowCount) newRow = 0;
            if (m_hasMic && newRow == static_cast<int>(m_rows.size())) {
                next = MicControl();
            } else {
                next = newRow * 2 + (onMic || focusedControl < 0 ? 0 : focusedControl % 2);
            }
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

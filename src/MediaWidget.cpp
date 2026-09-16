#include "MediaWidget.h"

#include <cwctype>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 40.0f;
constexpr float kRowGap = 4.0f;
constexpr float kBadgeSize = 24.0f;
constexpr float kButtonSize = 26.0f;
constexpr float kButtonGap = 4.0f;

// Numeric code points, not literal characters - see QuickActionsWidget.cpp.
constexpr wchar_t kTabGlyph[] = {0x266A, 0};    // ♪ - "media" in general, not one app
constexpr wchar_t kGlyphPrev[] = {0x23EE, 0};   // ⏮
constexpr wchar_t kGlyphPlay[] = {0x25B6, 0};   // ▶ - pause is drawn as vector bars, see Renderer
constexpr wchar_t kGlyphNext[] = {0x23ED, 0};   // ⏭

std::wstring ToLower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));
    return s;
}

std::wstring BadgeLetters(const std::wstring& name) {
    if (name.empty()) return L"?";
    return std::wstring(1, static_cast<wchar_t>(towupper(static_cast<wint_t>(name[0]))));
}

// No real icon extraction (that needs SHGetFileInfo/packaged-app manifest
// lookups per app, kept resident, for a purely decorative touch) - a
// recognizable flat color plus the app's initial is nearly free to draw and
// still reads as "this row is a different app" at a glance.
D2D1_COLOR_F BadgeColorFor(const std::wstring& name) {
    struct Entry {
        const wchar_t* match;
        D2D1_COLOR_F color;
    };
    static const Entry kKnown[] = {
        {L"spotify", D2D1::ColorF(0.11f, 0.73f, 0.33f)},
        {L"chrome", D2D1::ColorF(0.92f, 0.26f, 0.21f)},
        {L"msedge", D2D1::ColorF(0.00f, 0.54f, 0.83f)},
        {L"edge", D2D1::ColorF(0.00f, 0.54f, 0.83f)},
        {L"firefox", D2D1::ColorF(1.00f, 0.45f, 0.10f)},
        {L"vlc", D2D1::ColorF(0.95f, 0.55f, 0.10f)},
        {L"groove", D2D1::ColorF(0.85f, 0.15f, 0.55f)},
        {L"music", D2D1::ColorF(0.85f, 0.15f, 0.55f)},
    };
    std::wstring lower = ToLower(name);
    for (const auto& entry : kKnown) {
        if (lower.find(entry.match) != std::wstring::npos) return entry.color;
    }
    return D2D1::ColorF(0.36f, 0.39f, 0.46f); // neutral slate for anything unrecognized
}

D2D1_RECT_F RowRect(int row, float width) {
    float top = static_cast<float>(row) * (kRowHeight + kRowGap);
    return D2D1::RectF(0.0f, top, width, top + kRowHeight);
}

D2D1_RECT_F ButtonRect(D2D1_RECT_F row, int index) {
    float right = row.right - kPaddingX;
    float left = right - 3.0f * kButtonSize - 2.0f * kButtonGap;
    float x = left + static_cast<float>(index) * (kButtonSize + kButtonGap);
    float y = row.top + (kRowHeight - kButtonSize) / 2.0f;
    return D2D1::RectF(x, y, x + kButtonSize, y + kButtonSize);
}

D2D1_RECT_F BadgeRect(D2D1_RECT_F row) {
    float y = row.top + (kRowHeight - kBadgeSize) / 2.0f;
    return D2D1::RectF(kPaddingX, y, kPaddingX + kBadgeSize, y + kBadgeSize);
}

D2D1_RECT_F NameRect(D2D1_RECT_F row) {
    float left = kPaddingX + kBadgeSize + 8.0f;
    float right = ButtonRect(row, 0).left - 8.0f;
    return D2D1::RectF(left, row.top, right, row.bottom);
}
} // namespace

const wchar_t* MediaWidget::TabGlyph() const { return kTabGlyph; }
const wchar_t* MediaWidget::PanelTitle() const { return L"Media"; }

float MediaWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_rows.empty()) return kRowHeight;
    return static_cast<float>(m_rows.size()) * kRowHeight +
           static_cast<float>(m_rows.size() - 1) * kRowGap;
}

void MediaWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_rows.empty()) {
        D2D1_RECT_F rect = D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kRowHeight);
        painter.DrawLabel(rect, m_hasLoaded ? L"No media playing" : L"Checking for media...", true);
        return;
    }

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& r = m_rows[i];
        D2D1_RECT_F row = RowRect(i, width);

        painter.DrawBadge(BadgeRect(row), BadgeLetters(r.info.displayName).c_str(),
                           BadgeColorFor(r.info.displayName));
        painter.DrawLabel(NameRect(row), r.info.displayName.c_str(), false);

        bool prevEnabled = r.info.canPrevious && !r.busy;
        bool playEnabled = r.info.canPlayPause && !r.busy;
        bool nextEnabled = r.info.canNext && !r.busy;

        painter.DrawIconButton(ButtonRect(row, 0), kGlyphPrev, m_hoveredControl == i * 3 + 0,
                                prevEnabled);
        if (r.info.isPlaying) {
            painter.DrawPauseButton(ButtonRect(row, 1), m_hoveredControl == i * 3 + 1, playEnabled);
        } else {
            painter.DrawIconButton(ButtonRect(row, 1), kGlyphPlay, m_hoveredControl == i * 3 + 1,
                                    playEnabled);
        }
        painter.DrawIconButton(ButtonRect(row, 2), kGlyphNext, m_hoveredControl == i * 3 + 2,
                                nextEnabled);
    }
}

int MediaWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    if (m_rows.empty()) return -1;
    int row = static_cast<int>(y / (kRowHeight + kRowGap));
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return -1;

    D2D1_RECT_F rowRect = RowRect(row, width);
    if (y > rowRect.bottom) return -1; // landed in the gap between rows

    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F btn = ButtonRect(rowRect, i);
        if (x >= btn.left && x <= btn.right && y >= btn.top && y <= btn.bottom) {
            return row * 3 + i;
        }
    }
    return -1;
}

void MediaWidget::Activate(int controlId, HWND ownerHwnd) {
    if (m_rows.empty()) return;
    int row = controlId / 3;
    int button = controlId % 3;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;

    Row& r = m_rows[row];
    if (r.busy) return;

    MediaControls::Command command;
    bool enabled;
    switch (button) {
        case 0: command = MediaControls::Command::Previous; enabled = r.info.canPrevious; break;
        case 1: command = MediaControls::Command::PlayPause; enabled = r.info.canPlayPause; break;
        case 2: command = MediaControls::Command::Next; enabled = r.info.canNext; break;
        default: return;
    }
    if (!enabled) return;

    r.busy = true;
    // Pack both row and button into the tag so the reply (which may arrive
    // out of order relative to other in-flight commands) can flip the
    // play/pause icon immediately instead of waiting for the next refresh.
    MediaControls::SendCommand(r.handle, command, row * 3 + button, ownerHwnd,
                                kCommandResultMessage);
}

void MediaWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    if (message == kRefreshMessage) {
        auto* list = reinterpret_cast<MediaControls::SessionList*>(wParam);
        m_rows.clear();
        if (list) {
            for (size_t i = 0; i < list->infos.size(); ++i) {
                m_rows.push_back({list->infos[i], list->handles[i], false});
            }
            delete list;
        }
        m_hasLoaded = true;
        m_hoveredControl = -1;
        return;
    }

    if (message == kCommandResultMessage) {
        int tag = 0;
        MediaControls::Result result = MediaControls::Result::Failed;
        MediaControls::UnpackResult(wParam, tag, result);
        int row = tag / 3;
        int button = tag % 3;

        if (row >= 0 && row < static_cast<int>(m_rows.size())) {
            m_rows[row].busy = false;
            if (result == MediaControls::Result::Success && button == 1) {
                // Optimistic flip - avoids a second round-trip just to
                // re-read playback status after a toggle we know succeeded.
                m_rows[row].info.isPlaying = !m_rows[row].info.isPlaying;
            }
        }
        if (result == MediaControls::Result::Success) return;

        const wchar_t* text = L"Windows could not control this app.";
        if (result == MediaControls::Result::Unsupported) {
            text = L"This app does not support that control right now.";
        }
        MessageBoxW(nullptr, text, L"EdgeDeck", MB_ICONWARNING | MB_OK | MB_TOPMOST);
    }
}

void MediaWidget::OnPanelOpening(HWND ownerHwnd) {
    MediaControls::RefreshSessions(ownerHwnd, kRefreshMessage);
}

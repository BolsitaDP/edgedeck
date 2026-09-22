#include "LyricsWidget.h"

#include <algorithm>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kLineHeight = 22.0f;
constexpr float kHeaderHeight = 22.0f;
constexpr int kMaxVisibleLines = 10;
constexpr int kLinesBeforeCurrent = 2;

// Numeric code point, not a literal character - see QuickActionsWidget.cpp.
constexpr wchar_t kTabGlyph[] = {0x266B, 0}; // U+266B BEAMED EIGHTH NOTES
} // namespace

const wchar_t* LyricsWidget::TabGlyph() const { return kTabGlyph; }
const wchar_t* LyricsWidget::PanelTitle() const { return L"Lyrics"; }

float LyricsWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_state != State::Ready) return kLineHeight * 2.0f;
    int shown = std::min<int>(kMaxVisibleLines, static_cast<int>(m_lines.size()));
    return kHeaderHeight + static_cast<float>(shown) * kLineHeight;
}

void LyricsWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_state != State::Ready) {
        const wchar_t* message = L"Loading lyrics...";
        if (m_state == State::Empty) {
            switch (m_lastStatus) {
                case LrcLyrics::Status::NothingPlaying:
                    message = L"Nothing playing right now";
                    break;
                case LrcLyrics::Status::NoLyrics:
                    message = L"No synced lyrics found for this track";
                    break;
                default:
                    message = L"Could not reach the lyrics service";
                    break;
            }
        }
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kLineHeight * 2.0f),
                           message, true);
        return;
    }

    painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kHeaderHeight),
                       m_trackKey.c_str(), true);

    int total = static_cast<int>(m_lines.size());
    int shown = std::min(kMaxVisibleLines, total);
    int start = m_currentLine >= 0 ? m_currentLine - kLinesBeforeCurrent : 0;
    start = std::clamp(start, 0, std::max(0, total - shown));

    for (int i = 0; i < shown; ++i) {
        int idx = start + i;
        float y = kHeaderHeight + static_cast<float>(i) * kLineHeight;
        bool isCurrent = (idx == m_currentLine);
        painter.DrawLabel(D2D1::RectF(kPaddingX, y, width - kPaddingX, y + kLineHeight),
                           m_lines[idx].text.c_str(), !isCurrent);
    }
}

void LyricsWidget::OnPanelOpening(HWND ownerHwnd) {
    LrcLyrics::FetchCurrentLyrics(ownerHwnd, kFetchMessage);
}

void LyricsWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    if (message != kFetchMessage) return;
    auto* result = reinterpret_cast<LrcLyrics::Result*>(wParam);
    if (!result) return;

    m_lastStatus = result->status;
    if (result->status == LrcLyrics::Status::Success) {
        m_trackKey = result->trackKey;
        m_lines = std::move(result->lines);
        m_currentLine = -1;
        for (size_t i = 0; i < m_lines.size(); ++i) {
            if (m_lines[i].timeMs <= result->positionMs) {
                m_currentLine = static_cast<int>(i);
            } else {
                break;
            }
        }
        m_state = State::Ready;
    } else {
        m_lines.clear();
        m_trackKey.clear();
        m_currentLine = -1;
        m_state = State::Empty;
    }
    delete result;
}

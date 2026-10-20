#include "LyricsWidget.h"
#include "Strings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kLineHeight = 22.0f;
constexpr float kHeaderHeight = 24.0f;
constexpr int kMaxVisibleLines = 10;
constexpr int kLinesBeforeCurrent = 2;
constexpr int kLinesAfterCurrent = 5;
// Synced lyrics end with a row of their own for the timing: "Timing  [-] value [+]". On the
// header's line they took the room the track's name needs.
constexpr float kFooterHeight = 30.0f;
constexpr float kButtonWidth = 22.0f;
constexpr float kButtonHeight = 20.0f;
constexpr float kOffsetTextWidth = 46.0f;
constexpr float kControlsGap = 8.0f;
// After a click on a line, position samples that still say where the song was are ignored for
// this long: the player takes a moment to report the jump, and the highlight would bounce back.
constexpr ULONGLONG kSeekSettleMs = 1500;
constexpr int kSeekToleranceMs = 3000;
} // namespace

LyricsWidget::~LyricsWidget() {
    m_subscription.reset();
    m_requests.Clear();
}

const wchar_t* LyricsWidget::TabGlyph() const { return L"\x266B"; } // ♫
// U+E90B, lines of text with a note: lyrics, not just music.
const wchar_t* LyricsWidget::TabIcon() const {
    static constexpr wchar_t icon[] = {0xE90B, 0};
    return icon;
}

const wchar_t* LyricsWidget::PanelTitle() const { return TR(LyricsTitle); }

float LyricsWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_state != State::Ready) return kLineHeight * 2.0f;
    return FooterTop() + (Adjustable() ? kFooterHeight : 0.0f);
}

float LyricsWidget::FooterTop() const {
    const int shown = std::min<int>(kMaxVisibleLines, static_cast<int>(m_lines.size()));
    return HeaderHeight() + static_cast<float>(shown) * kLineHeight;
}

void LyricsWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_state != State::Ready) {
        const wchar_t* message = TR(LyricsLooking);
        if (m_state == State::Empty) {
            switch (m_lastStatus) {
                case LrcLyrics::Status::NothingPlaying:
                    message = TR(LyricsNothingPlaying);
                    break;
                case LrcLyrics::Status::NoLyrics:
                    message = TR(LyricsNone);
                    break;
                default:
                    message = TR(LyricsUnreachable);
                    break;
            }
        }
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kLineHeight * 2.0f),
                          message, true);
        return;
    }

    painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kHeaderHeight), m_trackKey.c_str(), true);
    if (Adjustable()) {
        const D2D1_RECT_F earlier = EarlierRect(width);
        const float labelTop = FooterTop() + (kFooterHeight - kLineHeight) / 2.0f + 2.0f;
        painter.DrawLabel(D2D1::RectF(kPaddingX, labelTop, earlier.left - kControlsGap, labelTop + kLineHeight),
                          TR(LyricsTiming), true);
        painter.DrawTextButton(earlier, L"\x2212", m_hovered == kEarlierControl); // minus sign
        painter.DrawValueText(OffsetTextRect(width), OffsetText().c_str(), m_offsetMs == 0);
        painter.DrawTextButton(LaterRect(width), L"+", m_hovered == kLaterControl);
    }
    // Plain lyrics say so, on a line of their own (beside the track it was cut off): there is no
    // line to light up, only the text, which keeps up with the song as a whole.
    if (!m_synced) {
        painter.DrawLabel(D2D1::RectF(kPaddingX, kHeaderHeight, width - kPaddingX, 2.0f * kHeaderHeight),
                          TR(LyricsNotSyncedNote), true);
    }

    const int total = static_cast<int>(m_lines.size());
    const int shown = std::min(kMaxVisibleLines, total);
    const int start = FirstShownLine();
    for (int i = 0; i < shown; ++i) {
        const int idx = start + i;
        const float y = HeaderHeight() + static_cast<float>(i) * kLineHeight;
        // Synced: the current line bright, the rest muted, and the one under the pointer - which a
        // click plays from - with the hover fill. Plain: all of it plain text.
        const bool muted = m_synced && idx != m_currentLine;
        const D2D1_RECT_F lineRect = D2D1::RectF(kPaddingX, y, width - kPaddingX, y + kLineHeight);
        if (m_synced && m_hovered == kLineControl + idx) painter.DrawHoverFill(lineRect);
        painter.DrawLabel(lineRect, m_lines[idx].text.c_str(), muted);
    }
}

int LyricsWidget::FirstShownLine() const {
    const int total = static_cast<int>(m_lines.size());
    const int shown = std::min(kMaxVisibleLines, total);
    const int last = std::max(0, total - shown);
    if (!m_synced) {
        if (m_durationMs <= 0) return 0;
        const double through = std::clamp(static_cast<double>(m_positionMs) / m_durationMs, 0.0, 1.0);
        return std::clamp(static_cast<int>(through * last + 0.5), 0, last);
    }
    // Centre the window on the current line, but only scroll once the current
    // line has actually left the comfortable middle of the view - otherwise the
    // whole block would creep upward one line at a time for the whole song.
    int desired = m_currentLine - kLinesBeforeCurrent;
    if (m_currentLine <= kLinesBeforeCurrent) desired = 0;
    if (m_currentLine >= total - kLinesAfterCurrent) desired = total - kLinesAfterCurrent;
    return std::clamp(desired, 0, last);
}

void LyricsWidget::OnPanelOpening(HWND ownerHwnd) {
    m_owner = ownerHwnd;

    // A track change is an event, not something to poll for: subscribing means
    // the panel re-fetches the moment the user skips, without a refresh ever
    // being requested by hand.
    if (!m_subscription) {
        m_subscription = MediaControls::SubscribeToChanges(ownerHwnd, kChangedMessage);
    }

    if (m_fetchRequestId != 0) return; // one lookup at a time
    m_state = State::Loading;
    m_fetchRequestId = m_requests.Begin();
    LrcLyrics::FetchCurrentLyrics(ownerHwnd, kFetchMessage, m_fetchRequestId);
}

void LyricsWidget::OnTick() {
    if (!m_owner || m_nowPlayingRequestId != 0) return;
    m_nowPlayingRequestId = m_requests.Begin();
    MediaControls::QueryNowPlaying(m_owner, kNowPlayingMessage, m_nowPlayingRequestId);
}

void LyricsWidget::UpdateCurrentLine(int positionMs) {
    m_positionMs = positionMs;
    m_currentLine = -1;
    if (!m_synced) return; // no times: FirstShownLine follows the position instead
    // A line shown m_offsetMs later than its time is current once the song is that far past it.
    const int lyricsMs = positionMs - m_offsetMs;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].timeMs <= lyricsMs) {
            m_currentLine = static_cast<int>(i);
        } else {
            break;
        }
    }
}

void LyricsWidget::ApplyTrack(const LrcLyrics::Result& result) {
    m_trackKey = result.trackKey;
    m_lines = result.lines;
    m_synced = result.synced;
    m_durationMs = result.durationMs;
    m_offsetMs = result.offsetMs;
    m_hovered = -1;
    UpdateCurrentLine(result.positionMs);
    m_state = State::Ready;
}

void LyricsWidget::ShowEmpty(LrcLyrics::Status status) {
    m_lastStatus = status;
    m_lines.clear();
    m_trackKey.clear();
    m_currentLine = -1;
    m_offsetMs = 0;
    m_hovered = -1;
    m_state = State::Empty;
}

D2D1_RECT_F LyricsWidget::LaterRect(float width) const {
    const float top = FooterTop() + (kFooterHeight - kButtonHeight) / 2.0f + 2.0f;
    return D2D1::RectF(width - kPaddingX - kButtonWidth, top, width - kPaddingX, top + kButtonHeight);
}

D2D1_RECT_F LyricsWidget::OffsetTextRect(float width) const {
    const D2D1_RECT_F later = LaterRect(width);
    return D2D1::RectF(later.left - kOffsetTextWidth, later.top, later.left, later.bottom);
}

D2D1_RECT_F LyricsWidget::EarlierRect(float width) const {
    const D2D1_RECT_F text = OffsetTextRect(width);
    return D2D1::RectF(text.left - kButtonWidth, text.top, text.left, text.bottom);
}

std::wstring LyricsWidget::OffsetText() const {
    if (m_offsetMs == 0) return L"0 s";
    wchar_t text[16];
    swprintf_s(text, L"%+.1f s", m_offsetMs / 1000.0);
    std::wstring value(text);
    if (Strings::Current() == Language::Spanish) std::replace(value.begin(), value.end(), L'.', L',');
    return value;
}

int LyricsWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    if (!Adjustable()) return -1;
    auto inside = [&](const D2D1_RECT_F& r) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
    if (inside(EarlierRect(width))) return kEarlierControl;
    if (inside(LaterRect(width))) return kLaterControl;

    if (y < HeaderHeight() || y >= FooterTop() || x < kPaddingX || x > width - kPaddingX) return -1;
    const int row = static_cast<int>((y - HeaderHeight()) / kLineHeight);
    const int shown = std::min(kMaxVisibleLines, static_cast<int>(m_lines.size()));
    if (row < 0 || row >= shown) return -1;
    const int idx = FirstShownLine() + row;
    // A blank line between verses is a gap, not something to play from.
    if (idx >= static_cast<int>(m_lines.size()) || m_lines[idx].text.empty()) return -1;
    return kLineControl + idx;
}

void LyricsWidget::ShiftLyrics(int deltaMs) {
    const int shifted = std::clamp(m_offsetMs + deltaMs, -LrcLyrics::kMaxOffsetMs, LrcLyrics::kMaxOffsetMs);
    if (shifted == m_offsetMs) return;
    m_offsetMs = shifted;
    UpdateCurrentLine(m_positionMs);
    LrcLyrics::SaveOffset(m_trackKey, m_offsetMs);
}

void LyricsWidget::Activate(int controlId, HWND /*ownerHwnd*/) {
    if (!Adjustable()) return;
    if (controlId == kEarlierControl) return ShiftLyrics(-LrcLyrics::kOffsetStepMs);
    if (controlId == kLaterControl) return ShiftLyrics(LrcLyrics::kOffsetStepMs);

    const int idx = controlId - kLineControl;
    if (idx < 0 || idx >= static_cast<int>(m_lines.size())) return;
    // The song is where this line is shown: its time, plus the correction.
    const int target = std::max(0, m_lines[idx].timeMs + m_offsetMs);
    MediaControls::SeekCurrent(target);
    m_seekTargetMs = target;
    m_seekedAt = GetTickCount64();
    UpdateCurrentLine(target);
}

bool LyricsWidget::OnStepControl(int controlId, int direction, float& outValue01) {
    if (controlId != 0 || !Adjustable()) return false;
    ShiftLyrics(direction > 0 ? LrcLyrics::kOffsetStepMs : -LrcLyrics::kOffsetStepMs);
    outValue01 = 0.5f;
    return true;
}

std::wstring LyricsWidget::AccessibleControlText(int controlId) const {
    if (!Adjustable()) return {};
    if (controlId == kEarlierControl) {
        return std::wstring(TR(LyricsEarlier)) + L". " + Strings::Format(Strings::Id::LyricsOffset, OffsetText().c_str());
    }
    if (controlId == kLaterControl) {
        return std::wstring(TR(LyricsLater)) + L". " + Strings::Format(Strings::Id::LyricsOffset, OffsetText().c_str());
    }
    const int idx = controlId - kLineControl;
    if (idx < 0 || idx >= static_cast<int>(m_lines.size())) return {};
    return Strings::Format(Strings::Id::LyricsPlayFrom, m_lines[idx].text.c_str());
}

void LyricsWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
    if (!envelope) return;
    auto discard = [&] { delete envelope; };

    if (message == kChangedMessage) {
        // Bare notification: take a fresh sample and let the handler below decide
        // whether the track actually changed. Not while the panel is closed: opening it looks
        // again anyway, and nothing should go to the network for a panel nobody sees.
        if (m_visible && m_owner && m_nowPlayingRequestId == 0) {
            m_nowPlayingRequestId = m_requests.Begin();
            MediaControls::QueryNowPlaying(m_owner, kNowPlayingMessage, m_nowPlayingRequestId);
        }
        return discard();
    }

    if (message == kFetchMessage) {
        if (envelope->Kind() != AsyncKind::Lyrics) return discard();
        auto* result = static_cast<LrcLyrics::Result*>(envelope);
        if (!m_requests.Accept(result->requestId)) return discard();
        m_fetchRequestId = 0;

        if (result->status == LrcLyrics::Status::Success) {
            m_failedAt = 0;
            ApplyTrack(*result);
        } else {
            m_failedAt = result->status == LrcLyrics::Status::NetworkError ? GetTickCount64() : 0;
            ShowEmpty(result->status);
        }
        return discard();
    }

    if (message == kNowPlayingMessage) {
        if (envelope->Kind() != AsyncKind::NowPlaying) return discard();
        auto* now = static_cast<MediaControls::NowPlaying*>(envelope);

        // A tick sample carries a request id we recognise; a change event no
        // longer reaches here at all, having been split off above.
        if (m_nowPlayingRequestId != 0) {
            if (!m_requests.Accept(now->requestId)) return discard();
            m_nowPlayingRequestId = 0;
        }

        if (!now->hasSession) {
            if (m_state != State::Empty) ShowEmpty(LrcLyrics::Status::NothingPlaying);
            return discard();
        }

        std::wstring key = now->artist.empty() ? now->title
                                                : (now->artist + L" - " + now->title);
        if (key.empty() || m_fetchRequestId != 0) return discard();

        if (m_state == State::Ready && key == m_trackKey) {
            // Same track, just a new position sample: move the highlight and
            // nothing else. (The length too, if the lookup did not have it: plain lyrics follow it.)
            if (m_durationMs <= 0 && now->durationMs > 0) m_durationMs = static_cast<int>(now->durationMs);
            const int position = static_cast<int>(now->positionMs);
            if (m_seekedAt != 0 && GetTickCount64() - m_seekedAt < kSeekSettleMs &&
                std::abs(position - m_seekTargetMs) > kSeekToleranceMs) {
                return discard();
            }
            m_seekedAt = 0;
            UpdateCurrentLine(position);
            return discard();
        }

        // Different track (or the first sample after a failed lookup): fetch
        // the new lyrics, caching first so a cached track costs no network. After a network
        // failure, not again for a while: every pause and skip is a change event.
        if (m_failedAt != 0 && GetTickCount64() - m_failedAt < kRetryAfterFailureMs) return discard();
        m_fetchRequestId = m_requests.Begin();
        LrcLyrics::FetchCurrentLyrics(m_owner, kFetchMessage, m_fetchRequestId);
        return discard();
    }

    discard();
}

std::wstring LyricsWidget::AccessibleSummary() const {
    if (m_state != State::Ready) return TR(LyricsSummaryNothing);
    if (m_currentLine < 0 || m_currentLine >= static_cast<int>(m_lines.size())) {
        return Strings::Format(Strings::Id::LyricsSummaryTrack, m_trackKey.c_str());
    }
    return Strings::Format(Strings::Id::LyricsSummaryLine, m_trackKey.c_str(), m_lines[m_currentLine].text.c_str());
}

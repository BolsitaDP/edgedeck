#include "LyricsWidget.h"

#include <algorithm>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kLineHeight = 22.0f;
constexpr float kHeaderHeight = 24.0f;
constexpr int kMaxVisibleLines = 10;
constexpr int kLinesBeforeCurrent = 2;
constexpr int kLinesAfterCurrent = 5;
} // namespace

LyricsWidget::~LyricsWidget() {
    m_subscription.reset();
    m_requests.Clear();
}

const wchar_t* LyricsWidget::TabGlyph() const { return L"\x266B"; } // ♫
const wchar_t* LyricsWidget::PanelTitle() const { return L"Lyrics"; }

float LyricsWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_state != State::Ready) return kLineHeight * 2.0f;
    int shown = std::min<int>(kMaxVisibleLines, static_cast<int>(m_lines.size()));
    return kHeaderHeight + static_cast<float>(shown) * kLineHeight;
}

void LyricsWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_state != State::Ready) {
        const wchar_t* message = L"Looking up lyrics...";
        if (m_state == State::Empty) {
            switch (m_lastStatus) {
                case LrcLyrics::Status::NothingPlaying:
                    message = L"Nothing playing right now";
                    break;
                case LrcLyrics::Status::NoLyrics:
                    message = L"No synced lyrics for this track";
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

    // Centre the window on the current line, but only scroll once the current
    // line has actually left the comfortable middle of the view - otherwise the
    // whole block would creep upward one line at a time for the whole song.
    int desired = m_currentLine - kLinesBeforeCurrent;
    if (m_currentLine <= kLinesBeforeCurrent) desired = 0;
    if (m_currentLine >= total - kLinesAfterCurrent) desired = total - kLinesAfterCurrent;
    int start = std::clamp(desired, 0, std::max(0, total - shown));

    for (int i = 0; i < shown; ++i) {
        int idx = start + i;
        float y = kHeaderHeight + static_cast<float>(i) * kLineHeight;
        bool isCurrent = (idx == m_currentLine);
        painter.DrawLabel(D2D1::RectF(kPaddingX, y, width - kPaddingX, y + kLineHeight),
                          m_lines[idx].text.c_str(), !isCurrent);
    }
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
    m_currentLine = -1;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].timeMs <= positionMs) {
            m_currentLine = static_cast<int>(i);
        } else {
            break;
        }
    }
}

void LyricsWidget::ApplyTrack(const std::wstring& trackKey, std::vector<LrcLyrics::Line> lines,
                              int positionMs) {
    m_trackKey = trackKey;
    m_lines = std::move(lines);
    UpdateCurrentLine(positionMs);
    m_state = State::Ready;
}

void LyricsWidget::ShowEmpty(LrcLyrics::Status status) {
    m_lastStatus = status;
    m_lines.clear();
    m_trackKey.clear();
    m_currentLine = -1;
    m_state = State::Empty;
}

void LyricsWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
    if (!envelope) return;
    auto discard = [&] { delete envelope; };

    if (message == kChangedMessage) {
        // Bare notification: take a fresh sample and let the handler below decide
        // whether the track actually changed.
        if (m_owner && m_nowPlayingRequestId == 0) {
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
            ApplyTrack(result->trackKey, std::move(result->lines), result->positionMs);
        } else {
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
            // nothing else.
            UpdateCurrentLine(static_cast<int>(now->positionMs));
            return discard();
        }

        // Different track (or the first sample after a failed lookup): fetch
        // the new lyrics, caching first so a cached track costs no network.
        m_fetchRequestId = m_requests.Begin();
        LrcLyrics::FetchCurrentLyrics(m_owner, kFetchMessage, m_fetchRequestId);
        return discard();
    }

    discard();
}

std::wstring LyricsWidget::AccessibleSummary() const {
    if (m_state != State::Ready) return L"Lyrics. Nothing to show.";
    if (m_currentLine < 0 || m_currentLine >= static_cast<int>(m_lines.size())) {
        return L"Lyrics for " + m_trackKey + L".";
    }
    return L"Lyrics for " + m_trackKey + L". " + m_lines[m_currentLine].text;
}

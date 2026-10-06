#include "Diagnostics.h"
#include "Renderer.h"
#include "MediaWidget.h"

#include <algorithm>
#include <cwctype>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 62.0f;
constexpr float kRowGap = 4.0f;
constexpr float kBadgeSize = 28.0f;
constexpr float kButtonSize = 26.0f;
constexpr float kButtonGap = 4.0f;
constexpr float kProgressHeight = 3.0f;

// Control id layout: row * 3 + button, where button is Prev / PlayPause / Next.
constexpr int kButtonsPerRow = 3;

float RowTop(int row) { return static_cast<float>(row) * (kRowHeight + kRowGap); }

D2D1_RECT_F RowRect(int row, float width) {
    float top = RowTop(row);
    return D2D1::RectF(0.0f, top, width, top + kRowHeight);
}

D2D1_RECT_F ButtonRect(D2D1_RECT_F row, int index) {
    float right = row.right - kPaddingX;
    float left = right - 3.0f * kButtonSize - 2.0f * kButtonGap;
    float x = left + static_cast<float>(index) * (kButtonSize + kButtonGap);
    float y = row.top + (kRowHeight - kButtonSize) / 2.0f - 6.0f;
    return D2D1::RectF(x, y, x + kButtonSize, y + kButtonSize);
}

D2D1_RECT_F BadgeRect(D2D1_RECT_F row) {
    float y = row.top + (kRowHeight - kBadgeSize) / 2.0f - 6.0f;
    return D2D1::RectF(kPaddingX, y, kPaddingX + kBadgeSize, y + kBadgeSize);
}

D2D1_RECT_F NameRect(D2D1_RECT_F row) {
    float left = kPaddingX + kBadgeSize + 10.0f;
    float right = ButtonRect(row, 0).left - 10.0f;
    return D2D1::RectF(left, row.top, right, row.top + kRowHeight / 2.0f);
}

D2D1_RECT_F TrackRect(D2D1_RECT_F row) {
    float left = kPaddingX + kBadgeSize + 10.0f;
    float right = row.right - kPaddingX;
    float top = row.top + kRowHeight / 2.0f + 2.0f;
    return D2D1::RectF(left, top, right, top + kProgressHeight);
}

D2D1_RECT_F ProgressRect(D2D1_RECT_F row) {
    D2D1_RECT_F track = TrackRect(row);
    float bottom = row.bottom - 8.0f;
    return D2D1::RectF(track.left, bottom - kProgressHeight, track.right, bottom);
}

// The first character of a display name, skipping anything that would render
// as mojibake: a lone high surrogate (the first half of an astral character),
// a combining mark or a zero-width joiner. The initial is only a decorative
// stand-in for a real app icon, so falling back is always better than showing
// replacement characters.
std::wstring BadgeLetters(const std::wstring& name) {
    size_t i = 0;
    while (i < name.size()) {
        wchar_t c = name[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < name.size() && name[i + 1] >= 0xDC00 &&
            name[i + 1] <= 0xDFFF) {
            // A whole astral character - perfectly renderable, use it as is.
            return name.substr(i, 2);
        }
        if (c == 0x200D || c == 0xFEFF || (c >= 0x0300 && c <= 0x036F)) {
            ++i;
            continue;
        }
        if (iswspace(c)) {
            ++i;
            continue;
        }
        return std::wstring(1, static_cast<wchar_t>(towupper(static_cast<wint_t>(c))));
    }
    return L"?";
}

std::wstring ToLowerAscii(std::wstring s) {
    for (wchar_t& c : s) {
        if (c < 128) c = static_cast<wchar_t>(::tolower(c));
    }
    return s;
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
        {L"spotify", D2D1::ColorF(0.11f, 0.73f, 0.33f)}, {L"chrome", D2D1::ColorF(0.92f, 0.26f, 0.21f)},
        {L"msedge", D2D1::ColorF(0.00f, 0.54f, 0.83f)},  {L"edge", D2D1::ColorF(0.00f, 0.54f, 0.83f)},
        {L"firefox", D2D1::ColorF(1.00f, 0.45f, 0.10f)}, {L"vlc", D2D1::ColorF(0.95f, 0.55f, 0.10f)},
        {L"groove", D2D1::ColorF(0.85f, 0.15f, 0.55f)}, {L"music", D2D1::ColorF(0.85f, 0.15f, 0.55f)},
    };
    std::wstring lower = ToLowerAscii(name);
    for (const auto& entry : kKnown) {
        if (lower.find(entry.match) != std::wstring::npos) return entry.color;
    }

    // Unknown app: derive a stable, reasonably saturated colour from the name
    // instead of collapsing every unknown source into the same slate, so two
    // unfamiliar apps are still told apart at a glance.
    unsigned long long hash = 1469598103934665603ULL; // FNV-1a
    for (wchar_t c : name) {
        hash ^= static_cast<unsigned long long>(c & 0x7F);
        hash *= 1099511628211ULL;
    }

    // HSL -> RGB by hand: D2D1 has no HSL helper, and going through HSL keeps
    // every generated colour at the same lightness and saturation, which is what
    // makes an arbitrary app name still look deliberate.
    //
    // The lightness follows the theme: a badge has to separate from the surface it
    // sits on, and one value cannot do that for both a #F3F3F3 and a #202020
    // panel. DrawBadge picks the letter colour from the result, so any lightness
    // here stays readable.
    const float hue = static_cast<float>(hash % 360) / 360.0f;
    const float saturation = PanelTheme::Current().dark ? 0.50f : 0.58f;
    const float lightness = PanelTheme::Current().dark ? 0.54f : 0.44f;

    auto channel = [&](float p, float q, float t) {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f / 2.0f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };

    const float q = lightness * (1.0f + saturation) - lightness * saturation;
    const float p = 2.0f * lightness - q;
    return D2D1::ColorF(channel(p, q, hue + 1.0f / 3.0f), channel(p, q, hue),
                        channel(p, q, hue - 1.0f / 3.0f), 1.0f);
}

std::wstring RowSubtitle(const MediaControls::SessionInfo& info) {
    if (!info.artist.empty() && !info.title.empty()) return info.artist + L" - " + info.title;
    if (!info.title.empty()) return info.title;
    if (!info.artist.empty()) return info.artist;
    return L"No track information";
}
} // namespace

MediaWidget::~MediaWidget() {
    // Drop the manager subscription before the tab's HWND goes away, so a late
    // event has nowhere to post to.
    m_subscription.reset();
    m_requests.Clear();
}

const wchar_t* MediaWidget::TabGlyph() const { return L"\x266A"; } // ♪
const wchar_t* MediaWidget::PanelTitle() const { return L"Media"; }

float MediaWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    if (m_state != State::Ready || m_rows.empty()) return 44.0f;
    return static_cast<float>(m_rows.size()) * kRowHeight +
           static_cast<float>(m_rows.size() - 1) * kRowGap;
}

void MediaWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    if (m_state != State::Ready || m_rows.empty()) {
        const wchar_t* message = m_state == State::Ready ? L"No media playing" : L"Checking...";
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kRowHeight), message, true);
        return;
    }

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& r = m_rows[i];
        D2D1_RECT_F row = RowRect(i, width);

        painter.DrawBadge(BadgeRect(row), BadgeLetters(r.info.displayName).c_str(),
                          BadgeColorFor(r.info.displayName));
        painter.DrawLabelPair(NameRect(row), r.info.displayName.c_str(),
                              RowSubtitle(r.info).c_str());

        // Progress: 0 for a live stream, otherwise played / total.
        D2D1_RECT_F progress = ProgressRect(row);
        bool indeterminate = r.info.durationMs <= 0;
        float value01 = indeterminate
                            ? 0.0f
                            : static_cast<float>(r.info.positionMs) /
                                  static_cast<float>(r.info.durationMs);
        painter.DrawProgress(progress, value01, indeterminate);

        bool busy = r.commandRequestId != 0;
        bool prevEnabled = r.info.canPrevious && !busy;
        bool playEnabled = r.info.canPlayPause && !busy;
        bool nextEnabled = r.info.canNext && !busy;

        int base = i * kButtonsPerRow;
        painter.DrawPrevButton(ButtonRect(row, 0), m_hoveredControl == base, prevEnabled);
        if (r.info.isPlaying) {
            painter.DrawPauseButton(ButtonRect(row, 1), m_hoveredControl == base + 1, playEnabled);
        } else {
            painter.DrawPlayButton(ButtonRect(row, 1), m_hoveredControl == base + 1, playEnabled);
        }
        painter.DrawNextButton(ButtonRect(row, 2), m_hoveredControl == base + 2, nextEnabled);
    }
}

int MediaWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    if (m_state != State::Ready || m_rows.empty()) return -1;
    if (y < 0.0f) return -1;
    int row = static_cast<int>(y / (kRowHeight + kRowGap));
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return -1;

    D2D1_RECT_F rowRect = RowRect(row, width);
    if (y > rowRect.bottom) return -1; // landed in the gap between rows

    for (int i = 0; i < kButtonsPerRow; ++i) {
        D2D1_RECT_F btn = ButtonRect(rowRect, i);
        if (x >= btn.left && x <= btn.right && y >= btn.top && y <= btn.bottom) {
            return row * kButtonsPerRow + i;
        }
    }
    return -1;
}

void MediaWidget::Activate(int controlId, HWND ownerHwnd) {
    if (controlId < 0 || m_state != State::Ready) return;
    int row = controlId / kButtonsPerRow;
    int button = controlId % kButtonsPerRow;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;

    Row& r = m_rows[row];
    if (r.commandRequestId != 0 || !r.handle) return; // a command is already in flight

    MediaControls::Command command;
    bool enabled;
    switch (button) {
        case 0:
            command = MediaControls::Command::Previous;
            enabled = r.info.canPrevious;
            break;
        case 1:
            command = MediaControls::Command::PlayPause;
            enabled = r.info.canPlayPause;
            break;
        case 2:
            command = MediaControls::Command::Next;
            enabled = r.info.canNext;
            break;
        default:
            return;
    }
    if (!enabled) return;

    r.commandRequestId = m_requests.Begin();
    MediaControls::SendCommand(r.handle, command, controlId, r.commandRequestId, m_listGeneration,
                               ownerHwnd, kCommandResultMessage);
}

void MediaWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
    if (!envelope) return;

    // Anything this widget did not ask for - including a reply belonging to a
    // previous panel open, or to a widget instance that has since been replaced
    // because Settings rebuilt the tabs - is dropped. Freeing it either way is
    // what keeps the ownership rule in one place.
    auto discard = [&] { delete envelope; };

    if (message == kChangedMessage) {
        if (m_owner) RequestRefresh(m_owner);
        return discard();
    }

    if (message == kRefreshMessage) {
        if (envelope->Kind() != AsyncKind::MediaSessions) return discard();
        auto* list = static_cast<MediaControls::SessionList*>(envelope);
        if (!m_requests.Accept(list->requestId) || list->listGeneration != m_listGeneration) {
            return discard();
        }
        m_refreshRequestId = 0;

        m_rows.clear();
        for (size_t i = 0; i < list->infos.size(); ++i) {
            Row r;
            r.info = list->infos[i];
            r.handle = list->handles[i];
            m_rows.push_back(std::move(r));
        }
        m_state = m_rows.empty() ? State::Empty : State::Ready;
        m_hoveredControl = -1;
        if (m_focusedControl >= static_cast<int>(m_rows.size()) * kButtonsPerRow) {
            m_focusedControl = -1;
        }
        return discard();
    }

    if (message == kCommandResultMessage) {
        if (envelope->Kind() != AsyncKind::MediaCommand) return discard();
        auto* result = static_cast<MediaControls::CommandResult*>(envelope);
        int row = result->tag / kButtonsPerRow;
        int button = result->tag % kButtonsPerRow;

        if (result->listGeneration == m_listGeneration && row >= 0 &&
            row < static_cast<int>(m_rows.size()) && m_rows[row].commandRequestId == result->requestId) {
            m_rows[row].commandRequestId = 0;
            if (result->code == static_cast<int>(MediaControls::Result::Success) && button == 1) {
                // Optimistic flip - avoids waiting for the next enumeration
                // just to re-read a playback state we know changed.
                m_rows[row].info.isPlaying = !m_rows[row].info.isPlaying;
            }
            m_requests.Retire(result->requestId);
            if (result->code == static_cast<int>(MediaControls::Result::Success)) return discard();
        }

        if (result->code == static_cast<int>(MediaControls::Result::Success)) return discard();

        const wchar_t* text = L"Windows could not control this app.";
        if (result->code == static_cast<int>(MediaControls::Result::Unsupported)) {
            text = L"This app does not support that control right now.";
        }
        ReportProblem(m_owner, text);
        return discard();
    }

    if (message == kNowPlayingMessage) {
        if (envelope->Kind() != AsyncKind::NowPlaying) return discard();
        auto* now = static_cast<MediaControls::NowPlaying*>(envelope);
        if (m_nowPlayingRequestId != 0 && m_requests.Accept(now->requestId)) {
            m_nowPlayingRequestId = 0;
            // Cheap interpolation between real samples, so the progress line
            // advances smoothly without re-enumerating sessions four times a
            // second. The next tick corrects it.
            for (Row& r : m_rows) {
                if (r.info.sourceId != now->sourceId) continue;
                r.info.isPlaying = now->isPlaying;
                if (now->durationMs > 0) r.info.durationMs = now->durationMs;
                r.info.positionMs = now->positionMs;
                break;
            }
        }
        discard();
        return;
    }

    discard();
}

void MediaWidget::OnPanelOpening(HWND ownerHwnd) {
    m_owner = ownerHwnd;
    EnsureSubscription(ownerHwnd);
    RequestRefresh(ownerHwnd);
}

void MediaWidget::OnPanelVisibilityChanged(bool visible) {
    if (visible && m_owner) RequestRefresh(m_owner);
}

void MediaWidget::OnTick() {
    if (!m_owner || m_nowPlayingRequestId != 0) return;
    m_nowPlayingRequestId = m_requests.Begin();
    MediaControls::QueryNowPlaying(m_owner, kNowPlayingMessage, m_nowPlayingRequestId);
}

void MediaWidget::RequestRefresh(HWND ownerHwnd) {
    if (m_refreshRequestId != 0) return; // one enumeration at a time

    // Some apps raise PlaybackInfoChanged on almost every state change, and a
    // full enumeration costs one round trip per session. Coalescing bursts into
    // one refresh keeps an open panel responsive without turning into a poll.
    ULONGLONG now = GetTickCount64();
    if (m_lastRefreshTick != 0 && now - m_lastRefreshTick < 400) return;
    m_lastRefreshTick = now;

    m_listGeneration++;
    m_refreshRequestId = m_requests.Begin();
    MediaControls::RefreshSessions(ownerHwnd, kRefreshMessage, m_refreshRequestId,
                                   m_listGeneration);
}

void MediaWidget::EnsureSubscription(HWND ownerHwnd) {
    if (m_subscription) return;
    m_subscription = MediaControls::SubscribeToChanges(ownerHwnd, kChangedMessage);
}

std::wstring MediaWidget::AccessibleSummary() const {
    if (m_state != State::Ready || m_rows.empty()) return L"Media. No media playing.";
    std::wstring out = L"Media. ";
    out += std::to_wstring(m_rows.size());
    out += (m_rows.size() == 1) ? L" app playing." : L" apps playing.";
    return out;
}

std::wstring MediaWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0) return {};
    int row = controlId / kButtonsPerRow;
    int button = controlId % kButtonsPerRow;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return {};

    const Row& r = m_rows[row];
    std::wstring track = RowSubtitle(r.info);
    switch (button) {
        case 0: return r.info.displayName + L": previous track. " + track;
        case 1:
            return r.info.displayName + L": " + (r.info.isPlaying ? L"pause" : L"play") + L". " +
                   track;
        case 2: return r.info.displayName + L": next track. " + track;
        default: return {};
    }
}

bool MediaWidget::OnKeyDown(UINT key, int focusedControl) {
    if (m_state != State::Ready || m_rows.empty()) return false;
    int total = static_cast<int>(m_rows.size()) * kButtonsPerRow;
    if (total <= 0) return false;

    int next = focusedControl;
    switch (key) {
        case VK_LEFT: next = focusedControl > 0 ? focusedControl - 1 : total - 1; break;
        case VK_RIGHT: next = focusedControl + 1 < total ? focusedControl + 1 : 0; break;
        case VK_UP:
        case VK_DOWN: {
            int row = focusedControl / kButtonsPerRow;
            int button = focusedControl % kButtonsPerRow;
            int rowDelta = (key == VK_UP) ? -1 : 1;
            int newRow = row + rowDelta;
            if (newRow < 0) newRow = static_cast<int>(m_rows.size()) - 1;
            if (newRow >= static_cast<int>(m_rows.size())) newRow = 0;
            next = newRow * kButtonsPerRow + button;
            break;
        }
        case VK_SPACE:
        case VK_RETURN:
            if (focusedControl >= 0) Activate(focusedControl, m_owner);
            return true;
        default: return false;
    }

    m_focusedControl = next;
    m_hoveredControl = next;
    return true;
}

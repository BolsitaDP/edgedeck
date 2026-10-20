#include "LrcLyrics.h"
#include "MediaManager.h"
#include "MediaPosition.h"
#include "Version.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Data.Json.h>

#include <winhttp.h>

#include <algorithm>
#include <mutex>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace LrcLyrics {
namespace {

using Manager = winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonValueType;

// How long a "this track has no synced lyrics" answer is trusted before we ask
// LRCLIB again. Without this, every single panel open on an unsynced track is
// another HTTP request to a free, volunteer-run public service.
constexpr long long kNegativeCacheTtlMs = 7LL * 24 * 60 * 60 * 1000;
// The extension of a "no lyrics" marker. Markers used to end in .none - but the lookup that wrote
// them threw away every result list whose first record had no synced lyrics, and plain lyrics
// altogether, so those markers say nothing true and are ignored by being under another name.
constexpr const wchar_t* kNoLyricsMarker = L".nolyrics";

// A lyrics response is a few KB; anything past this is not a lyrics file and
// buffering it would just be a way to exhaust memory.
constexpr size_t kMaxResponseBytes = 4u * 1024u * 1024u;

// ---------------------------------------------------------------------------
// Small helpers: paths, on-disk cache
// ---------------------------------------------------------------------------

// FILETIME counts 100ns ticks from 1601-01-01; the cache wants milliseconds
// since the Unix epoch so the values are comparable across machines.
constexpr long long kUnixEpochInFileTimeTicks = 116444736000000000LL;

long long FileTimeToUnixMs(FILETIME ft) {
    long long ticks = (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (ticks - kUnixEpochInFileTimeTicks) / 10000;
}

std::wstring CacheDir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";
    std::wstring edgeDeckDir = std::wstring(buf) + L"\\EdgeDeck";
    CreateDirectoryW(edgeDeckDir.c_str(), nullptr);
    std::wstring dir = edgeDeckDir + L"\\lyrics_cache";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring SanitizeForFilename(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        // Anything outside the filename-safe set, plus control characters, so a
        // track title can never produce a reserved device name (CON, NUL, ...).
        bool bad = c < 0x20 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
                   c == L'"' || c == L'<' || c == L'>' || c == L'|';
        out.push_back(bad ? L'_' : c);
    }
    if (out.empty()) return L"unknown";
    if (out.size() <= 4) return out;
    static const wchar_t* kReserved[] = {L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3",
                                         L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
                                         L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6",
                                         L"LPT7", L"LPT8", L"LPT9"};
    for (const wchar_t* reserved : kReserved) {
        if (out.compare(0, 4, reserved) == 0) return L"_" + out;
    }
    return out;
}

std::wstring CachePathFor(const std::wstring& trackKey, const wchar_t* extension) {
    std::wstring dir = CacheDir();
    if (dir.empty()) return L"";
    return dir + L"\\" + SanitizeForFilename(trackKey) + extension;
}

std::wstring Utf8ToWide(const std::string& s); // below, with the other text helpers

// The cache is UTF-8 text, read and written as bytes. It used to go through std::wofstream in the
// "C" locale, which cannot write a character outside ASCII: the first accent in a title or a line -
// "Aragón", "corazón" - failed the write, and Spanish lyrics were never cached at all.
bool ReadUtf8File(const std::wstring& path, std::wstring& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 2 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = bytes.empty() || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    bytes.resize(read);
    out = Utf8ToWide(bytes);
    return true;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    std::string bytes;
    if (!text.empty()) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                               nullptr, nullptr);
        if (length <= 0) return false;
        bytes.assign(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), length, nullptr,
                            nullptr);
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

bool ReadCache(const std::wstring& trackKey, std::vector<Line>& outLines, bool& synced, int& offsetMs) {
    std::wstring path = CachePathFor(trackKey, L".txt");
    std::wstring text;
    if (path.empty() || !ReadUtf8File(path, text)) return false;
    return ParseCache(text, outLines, synced, &offsetMs);
}

// A fetch and a correction can both write one track's file: one writer at a time, so neither
// takes the other's temp file.
std::mutex g_cacheWrite;

void WriteCache(const std::wstring& trackKey, const std::vector<Line>& lines, bool synced, int offsetMs = 0) {
    std::wstring path = CachePathFor(trackKey, L".txt");
    if (path.empty()) return;
    std::lock_guard<std::mutex> lock(g_cacheWrite);

    // Written to a sibling temp file and swapped in, so a crash or a full disk
    // halfway through can't leave a half-written lyrics file that would then be
    // served from cache forever.
    std::wstring temp = path + L".tmp";
    if (!WriteUtf8File(temp, SerializeCache(lines, synced, offsetMs))) {
        DeleteFileW(temp.c_str());
        return;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
    }
}
// "we already asked LRCLIB about this track and it had nothing" marker, with the
// time of the query so the marker can expire.
bool ReadNegativeCache(const std::wstring& trackKey) {
    std::wstring path = CachePathFor(trackKey, kNoLyricsMarker);
    if (path.empty()) return false;
    std::wifstream file(path);
    if (!file) return false;

    long long queriedAtMs = 0;
    if (!(file >> queriedAtMs)) return false;

    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    long long nowMs = FileTimeToUnixMs(ft);

    long long age = nowMs - queriedAtMs;
    // A marker stamped in the future (clock moved, cache copied between machines)
    // is not trustworthy either.
    if (age < 0 || age > kNegativeCacheTtlMs) return false;
    return true;
}

void WriteNegativeCache(const std::wstring& trackKey) {
    std::wstring path = CachePathFor(trackKey, kNoLyricsMarker);
    if (path.empty()) return;
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);

    std::wofstream file(path, std::ios::trunc);
    if (file) file << FileTimeToUnixMs(ft) << L"\n";
}

// ---------------------------------------------------------------------------
// UTF-8 / percent-encoding for the LRCLIB query string
// ---------------------------------------------------------------------------

std::string Utf8FromWide(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0,
                                   nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len,
                         nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::wstring UrlEncode(const std::wstring& s) {
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::string utf8 = Utf8FromWide(s);
    std::wstring out;
    out.reserve(utf8.size() * 3);
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            out.push_back(L'%');
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// LRC text ("[mm:ss.xx]lyric line") -> structured lines
// ---------------------------------------------------------------------------

// Converts the fractional-seconds field to milliseconds regardless of how many
// digits it actually has: ".5" is 500ms, ".50" is 500ms, ".500" is 500ms. The
// old code assumed two digits and read one-digit fractions as tens of ms.
int FractionToMs(const std::wstring& frac) {
    if (frac.empty()) return 0;
    int value = _wtoi(frac.c_str());
    int digits = 0;
    for (wchar_t c : frac) {
        if (!iswdigit(c)) break;
        ++digits;
    }
    if (digits <= 0) return 0;
    if (digits > 3) digits = 3; // LRC has no finer resolution than milliseconds
    int scale = 1;
    for (int i = 0; i < 3 - digits; ++i) scale *= 10;
    return value * scale;
}

} // namespace

std::vector<Line> ParseLrc(const std::wstring& lrc) {
    std::vector<Line> lines;
    std::wistringstream stream(lrc);
    std::wstring rawLine;
    int offsetMs = 0;
    bool sawOffsetTag = false;

    while (std::getline(stream, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == L'\r') rawLine.pop_back();

        std::vector<int> times;
        size_t pos = 0;
        bool tagRunEnded = false;

        while (pos < rawLine.size() && rawLine[pos] == L'[' && !tagRunEnded) {
            size_t close = rawLine.find(L']', pos);
            if (close == std::wstring::npos) break;

            const std::wstring tag = rawLine.substr(pos + 1, close - pos - 1);

            // [offset:...] has to be recognised before the generic timestamp
            // test below, because it contains a colon too and would otherwise
            // be misread as a malformed timestamp and treated as lyric text.
            if (tag.size() > 7 && tag.compare(0, 7, L"offset:") == 0) {
                const std::wstring value = tag.substr(7);
                if (!value.empty() && (value[0] == L'+' || value[0] == L'-')) {
                    offsetMs = _wtoi(value.c_str());
                    sawOffsetTag = true;
                }
                pos = close + 1;
                continue;
            }

            const size_t colon = tag.find(L':');
            const size_t dot = tag.find(L'.');

            if (colon != std::wstring::npos && dot != std::wstring::npos && colon < dot) {
                const int mm = _wtoi(tag.substr(0, colon).c_str());
                const int ss = _wtoi(tag.substr(colon + 1, dot - colon - 1).c_str());
                times.push_back(mm * 60000 + ss * 1000 + FractionToMs(tag.substr(dot + 1)));
            } else {
                // Any other bracketed tag - [ar:...], [ti:...], [length:...], or
                // a path-style [00:12] with no fraction. The rest of the line is
                // lyric text, not more tags.
                tagRunEnded = true;
                break;
            }
            pos = close + 1;
        }

        if (times.empty()) continue;

        // One copy per timestamp: "[00:12.00][01:30.00]chorus" means the same
        // line recurs, and keeping only the last stamp silently lost the first.
        std::wstring text = rawLine.substr(pos);
        size_t firstNonSpace = text.find_first_not_of(L" \t");
        if (firstNonSpace == std::wstring::npos) continue;
        text = text.substr(firstNonSpace);
        while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) text.pop_back();

        for (int time : times) {
            int adjusted = time + (sawOffsetTag ? offsetMs : 0);
            if (adjusted < 0) adjusted = 0;
            lines.push_back({adjusted, text});
        }
    }

    std::stable_sort(lines.begin(), lines.end(),
                     [](const Line& a, const Line& b) { return a.timeMs < b.timeMs; });
    return lines;
}

Choice PickLyrics(const std::vector<Candidate>& candidates, int durationMs) {
    auto distance = [&](const Candidate& c) {
        // An unknown length on either side matches anything, equally well.
        if (durationMs <= 0 || c.durationMs <= 0) return 0;
        return std::abs(c.durationMs - durationMs);
    };
    int bestSynced = -1;
    int bestPlain = -1;
    int anySynced = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates[i];
        if (c.instrumental) continue;
        const int index = static_cast<int>(i);
        if (!c.synced.empty()) {
            if (anySynced < 0 || distance(c) < distance(candidates[static_cast<size_t>(anySynced)])) anySynced = index;
            if (distance(c) <= kDurationToleranceMs &&
                (bestSynced < 0 || distance(c) < distance(candidates[static_cast<size_t>(bestSynced)]))) {
                bestSynced = index;
            }
        }
        if (!c.plain.empty() &&
            (bestPlain < 0 || distance(c) < distance(candidates[static_cast<size_t>(bestPlain)]))) {
            bestPlain = index;
        }
    }
    if (bestSynced >= 0) return {bestSynced, true};
    if (bestPlain >= 0) return {bestPlain, false};
    if (anySynced >= 0) return {anySynced, true};
    return {};
}

std::vector<Line> PlainLines(const std::wstring& text) {
    std::vector<Line> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) line.pop_back();
        const bool blank = line.find_first_not_of(L" \t") == std::wstring::npos;
        if (!blank) {
            lines.push_back({-1, line});
        } else if (!lines.empty() && !lines.back().text.empty()) {
            lines.push_back({-1, L""});
        }
        start = end + 1;
    }
    while (!lines.empty() && lines.back().text.empty()) lines.pop_back();
    return lines;
}

std::wstring SerializeCache(const std::vector<Line>& lines, bool synced, int offsetMs) {
    std::wstring text = synced ? L"" : L"#plain\n";
    if (offsetMs != 0) text += L"#offset\t" + std::to_wstring(offsetMs) + L"\n";
    for (const Line& l : lines) text += std::to_wstring(l.timeMs) + L"\t" + l.text + L"\n";
    return text;
}

bool ParseCache(const std::wstring& text, std::vector<Line>& lines, bool& synced, int* offsetMs) {
    lines.clear();
    synced = true;
    if (offsetMs) *offsetMs = 0;
    size_t start = 0;
    bool first = true;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (first && line == L"#plain") {
            synced = false;
            first = false;
            continue;
        }
        first = false;
        if (line.rfind(L"#offset\t", 0) == 0) {
            if (offsetMs) *offsetMs = std::clamp(_wtoi(line.c_str() + 8), -kMaxOffsetMs, kMaxOffsetMs);
            continue;
        }
        const size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        lines.push_back({_wtoi(line.substr(0, tab).c_str()), line.substr(tab + 1)});
    }
    return !lines.empty();
}
namespace {

// ---------------------------------------------------------------------------
// Minimal synchronous WinHTTP GET - fine here since this only ever runs
// already off the UI thread (inside winrt::resume_background).
// ---------------------------------------------------------------------------

struct HttpResponse {
    bool ok = false;
    int status = 0;
    bool truncated = false;
    std::string body;
};

// One session for the whole process, so connections to lrclib.net are reused
// across fetches instead of a full TCP + TLS handshake per panel open.
HINTERNET SharedSession() {
    static HINTERNET session = [] {
        // LRCLIB asks every client to say who it is - name, version and where to find it - so its
        // maintainers can tell traffic apart and know whom to contact.
        HINTERNET s = WinHttpOpen(L"EdgeDeck " EDGEDECK_VERSION_WSTRING L" (" EDGEDECK_HOMEPAGE_WSTRING L")",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!s) return static_cast<HINTERNET>(nullptr);

        // Without explicit timeouts WinHTTP defaults to ~60s to connect, which
        // would pin a thread-pool thread for a minute on a bad network - and
        // every panel open can start another one. A lyrics lookup that hasn't
        // answered in a few seconds is not coming.
        WinHttpSetTimeouts(s, 5000 /*resolve*/, 5000 /*connect*/, 5000 /*send*/,
                           8000 /*receive*/);
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(s, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
        return s;
    }();
    return session;
}

HttpResponse HttpsGet(const wchar_t* host, const std::wstring& path) {
    HttpResponse resp;

    HINTERNET hSession = SharedSession();
    if (!hSession) return resp;

    HINTERNET hConnect =
        WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) return resp;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return resp;
    }

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                            0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0, statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                            WINHTTP_NO_HEADER_INDEX);
        resp.status = static_cast<int>(statusCode);
        resp.ok = true;

        DWORD available = 0;
        while (resp.body.size() < kMaxResponseBytes &&
               WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
            DWORD toRead = available;
            size_t room = kMaxResponseBytes - resp.body.size();
            if (toRead > room) {
                toRead = static_cast<DWORD>(room);
                resp.truncated = true;
            }
            size_t offset = resp.body.size();
            resp.body.resize(offset + toRead);
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, resp.body.data() + offset, toRead, &bytesRead)) {
                resp.body.resize(offset);
                break;
            }
            resp.body.resize(offset + bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return resp;
}

// Hands the result to the UI thread exactly once, on every exit path.
//
// This exists because `co_return` skips whatever follows the try block, so an
// early exit used to step straight over the post: the envelope was orphaned and
// the widget sat on "Looking up lyrics..." for the rest of the session. Nearly
// every outcome takes an early exit here - a cache hit, the negative cache, no
// session, an HTTP failure, "no synced lyrics for this track" - so only a live
// successful fetch ever completed. As a coroutine local it lives in the frame,
// so the destructor runs when the coroutine finishes by any route, including
// returning early or unwinding.
class PostOnExit {
public:
    PostOnExit(HWND window, UINT message, Result* result) noexcept
        : m_window(window), m_message(message), m_result(result) {}

    ~PostOnExit() {
        if (m_result) PostOrDelete(m_window, m_message, m_result);
    }

    PostOnExit(const PostOnExit&) = delete;
    PostOnExit& operator=(const PostOnExit&) = delete;

    // Give up ownership without posting, for a path that has already sent the
    // result some other way. Unused today; here so the "posted twice" mistake is
    // a one-liner if it ever is needed.
    void Release() noexcept { m_result = nullptr; }

private:
    HWND m_window;
    UINT m_message;
    Result* m_result;
};

winrt::fire_and_forget FetchAsync(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId) {
    auto* result = new Result();
    result->requestId = requestId;
    PostOnExit post(notifyWindow, notifyMessage, result);

    try {
        co_await winrt::resume_background();

        // Whatever Windows itself currently considers the "now playing" app -
        // same pick MediaWidget uses.
        Manager manager = co_await MediaManager::GetAsync();
        auto session = manager.GetCurrentSession();
        if (!session) {
            result->status = Status::NothingPlaying;
            co_return;
        }

        auto mediaProps = co_await session.TryGetMediaPropertiesAsync();
        std::wstring title = mediaProps.Title().c_str();
        std::wstring artist = mediaProps.Artist().c_str();
        const auto timeline = session.GetTimelineProperties();
        result->positionMs = static_cast<int>(MediaPosition::NowMs(session));
        const long long lengthMs = (timeline.EndTime() - timeline.StartTime()).count() / 10000;
        result->durationMs = lengthMs > 0 ? static_cast<int>(lengthMs) : 0;

        if (title.empty()) {
            result->status = Status::NothingPlaying;
            co_return;
        }

        std::wstring trackKey = artist.empty() ? title : (artist + L" - " + title);
        result->trackKey = trackKey;

        // Cache hit: done, no network call at all.
        if (ReadCache(trackKey, result->lines, result->synced, result->offsetMs)) {
            result->status = Status::Success;
            co_return;
        }

        // Known-missing: also done, and crucially without touching the network.
        if (ReadNegativeCache(trackKey)) {
            result->status = Status::NoLyrics;
            co_return;
        }

        std::wstring path = L"/api/search?track_name=" + UrlEncode(title);
        if (!artist.empty()) path += L"&artist_name=" + UrlEncode(artist);

        HttpResponse resp = HttpsGet(L"lrclib.net", path);
        if (!resp.ok || resp.status != 200) {
            result->status = Status::NetworkError;
            co_return;
        }

        // Every record's fields are read one by one, and a field that is missing or JSON null is
        // simply empty. LRCLIB sends "syncedLyrics": null for a song it has only plain lyrics for,
        // and the getter with a default throws on a null that is there - which threw away the
        // whole list whenever its first record had no synced lyrics, and cached "none" for a week.
        std::vector<Candidate> candidates;
        try {
            const JsonArray list = JsonArray::Parse(Utf8ToWide(resp.body));
            for (auto const& item : list) {
                if (item.ValueType() != JsonValueType::Object) continue;
                const auto obj = item.GetObject();
                auto text = [&](const wchar_t* name) {
                    const auto value = obj.TryLookup(name);
                    return (value && value.ValueType() == JsonValueType::String) ? std::wstring(value.GetString())
                                                                                 : std::wstring();
                };
                Candidate c;
                c.synced = text(L"syncedLyrics");
                c.plain = text(L"plainLyrics");
                if (const auto value = obj.TryLookup(L"instrumental"); value && value.ValueType() == JsonValueType::Boolean) {
                    c.instrumental = value.GetBoolean();
                }
                if (const auto value = obj.TryLookup(L"duration"); value && value.ValueType() == JsonValueType::Number) {
                    c.durationMs = static_cast<int>(value.GetNumber() * 1000.0);
                }
                candidates.push_back(std::move(c));
            }
        } catch (...) {
            // Not a JSON array at all: nothing found, rather than a network failure.
            candidates.clear();
        }

        const Choice choice = PickLyrics(candidates, result->durationMs);
        std::vector<Line> lines;
        if (choice.index >= 0) {
            const Candidate& picked = candidates[static_cast<size_t>(choice.index)];
            lines = choice.synced ? ParseLrc(picked.synced) : PlainLines(picked.plain);
        }
        if (lines.empty()) {
            WriteNegativeCache(trackKey);
            result->status = Status::NoLyrics;
            co_return;
        }

        WriteCache(trackKey, lines, choice.synced);
        result->synced = choice.synced;
        result->lines = std::move(lines);
        result->status = Status::Success;
    } catch (...) {
        result->status = Status::NetworkError;
    }

    // The result is posted by post, on the way out of every path above.
}

// Corrections waiting to be written, the newest per track. A burst of clicks starts a burst of
// writers; one writes at a time and each takes whatever is newest when it gets its turn, so the
// file ends with the last value whatever order they run in. The list has its own lock, held only
// to touch the list, so a click never waits for a disk.
std::mutex g_offsetsMutex;
std::mutex g_offsetWriter;
std::vector<std::pair<std::wstring, int>> g_pendingOffsets;

winrt::fire_and_forget SaveOffsetAsync() {
    try {
        co_await winrt::resume_background();
    } catch (...) {
        co_return;
    }
    std::lock_guard<std::mutex> writing(g_offsetWriter);
    for (;;) {
        std::pair<std::wstring, int> next;
        {
            std::lock_guard<std::mutex> lock(g_offsetsMutex);
            if (g_pendingOffsets.empty()) break;
            next = std::move(g_pendingOffsets.back());
            g_pendingOffsets.pop_back();
        }
        const auto& [trackKey, offsetMs] = next;
        std::vector<Line> lines;
        bool synced = true;
        int previous = 0;
        if (ReadCache(trackKey, lines, synced, previous) && previous != offsetMs) {
            WriteCache(trackKey, lines, synced, offsetMs);
        }
    }
}

} // namespace

void FetchCurrentLyrics(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId) {
    FetchAsync(notifyWindow, notifyMessage, requestId);
}

void SaveOffset(const std::wstring& trackKey, int offsetMs) {
    {
        std::lock_guard<std::mutex> lock(g_offsetsMutex);
        std::erase_if(g_pendingOffsets, [&](const auto& pending) { return pending.first == trackKey; });
        g_pendingOffsets.emplace_back(trackKey, std::clamp(offsetMs, -kMaxOffsetMs, kMaxOffsetMs));
    }
    SaveOffsetAsync();
}

} // namespace LrcLyrics

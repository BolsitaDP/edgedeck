#include "LrcLyrics.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Data.Json.h>

#include <winhttp.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace LrcLyrics {
namespace {

using Manager = winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Data::Json::JsonArray;

// How long a "this track has no synced lyrics" answer is trusted before we ask
// LRCLIB again. Without this, every single panel open on an unsynced track is
// another HTTP request to a free, volunteer-run public service.
constexpr long long kNegativeCacheTtlMs = 7LL * 24 * 60 * 60 * 1000;

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

bool ReadCache(const std::wstring& trackKey, std::vector<Line>& outLines) {
    std::wstring path = CachePathFor(trackKey, L".txt");
    if (path.empty()) return false;
    std::wifstream file(path);
    if (!file) return false;

    std::wstring line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        outLines.push_back({_wtoi(line.substr(0, tab).c_str()), line.substr(tab + 1)});
    }
    return !outLines.empty();
}

void WriteCache(const std::wstring& trackKey, const std::vector<Line>& lines) {
    std::wstring path = CachePathFor(trackKey, L".txt");
    if (path.empty()) return;

    // Written to a sibling temp file and swapped in, so a crash or a full disk
    // halfway through can't leave a half-written lyrics file that would then be
    // served from cache forever.
    std::wstring temp = path + L".tmp";
    {
        std::wofstream file(temp, std::ios::trunc);
        if (!file) return;
        for (const auto& l : lines) file << l.timeMs << L'\t' << l.text << L'\n';
        if (!file) {
            file.close();
            DeleteFileW(temp.c_str());
            return;
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
    }
}

// "we already asked LRCLIB about this track and it had nothing" marker, with the
// time of the query so the marker can expire.
bool ReadNegativeCache(const std::wstring& trackKey) {
    std::wstring path = CachePathFor(trackKey, L".none");
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
    std::wstring path = CachePathFor(trackKey, L".none");
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
        HINTERNET s = WinHttpOpen(L"EdgeDeck/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
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
        Manager manager = co_await Manager::RequestAsync();
        auto session = manager.GetCurrentSession();
        if (!session) {
            result->status = Status::NothingPlaying;
            co_return;
        }

        auto mediaProps = co_await session.TryGetMediaPropertiesAsync();
        std::wstring title = mediaProps.Title().c_str();
        std::wstring artist = mediaProps.Artist().c_str();
        result->positionMs =
            static_cast<int>(session.GetTimelineProperties().Position().count() / 10000);

        if (title.empty()) {
            result->status = Status::NothingPlaying;
            co_return;
        }

        std::wstring trackKey = artist.empty() ? title : (artist + L" - " + title);
        result->trackKey = trackKey;

        // Cache hit: done, no network call at all.
        if (ReadCache(trackKey, result->lines)) {
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

        std::wstring synced;
        try {
            JsonArray candidates = JsonArray::Parse(Utf8ToWide(resp.body));
            for (auto const& candidate : candidates) {
                auto obj = candidate.GetObject();
                if (obj.GetNamedBoolean(L"instrumental", false)) continue;
                std::wstring s = obj.GetNamedString(L"syncedLyrics", L"").c_str();
                if (!s.empty()) {
                    synced = s;
                    break;
                }
            }
        } catch (...) {
            // Malformed or unexpected payload: treat as "nothing found" rather
            // than as a network failure, and remember that so we don't re-fetch.
            synced.clear();
        }

        if (synced.empty()) {
            WriteNegativeCache(trackKey);
            result->status = Status::NoLyrics;
            co_return;
        }

        std::vector<Line> lines = ParseLrc(synced);
        if (lines.empty()) {
            WriteNegativeCache(trackKey);
            result->status = Status::NoLyrics;
            co_return;
        }

        WriteCache(trackKey, lines);
        result->lines = std::move(lines);
        result->status = Status::Success;
    } catch (...) {
        result->status = Status::NetworkError;
    }

    // The result is posted by post, on the way out of every path above.
}

} // namespace

void FetchCurrentLyrics(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId) {
    FetchAsync(notifyWindow, notifyMessage, requestId);
}

} // namespace LrcLyrics

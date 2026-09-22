#include "LrcLyrics.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Data.Json.h>

#include <winhttp.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "winhttp.lib")

namespace LrcLyrics {
namespace {

using Manager = winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Data::Json::JsonArray;

// ---------------------------------------------------------------------------
// Small helpers: paths, on-disk cache
// ---------------------------------------------------------------------------

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
        bool bad = c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' ||
                   c == L'<' || c == L'>' || c == L'|';
        out.push_back(bad ? L'_' : c);
    }
    return out.empty() ? L"unknown" : out;
}

std::wstring CachePathFor(const std::wstring& trackKey) {
    std::wstring dir = CacheDir();
    return dir.empty() ? L"" : dir + L"\\" + SanitizeForFilename(trackKey) + L".txt";
}

bool ReadCache(const std::wstring& trackKey, std::vector<Line>& outLines) {
    std::wstring path = CachePathFor(trackKey);
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
    std::wstring path = CachePathFor(trackKey);
    if (path.empty()) return;
    std::wofstream file(path, std::ios::trunc);
    if (!file) return;
    for (const auto& l : lines) file << l.timeMs << L'\t' << l.text << L'\n';
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
// LRC text ("[mm:ss.xx]lyric line", one per line) -> structured lines
// ---------------------------------------------------------------------------

std::vector<Line> ParseLrc(const std::wstring& lrc) {
    std::vector<Line> lines;
    std::wistringstream stream(lrc);
    std::wstring rawLine;

    while (std::getline(stream, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == L'\r') rawLine.pop_back();

        size_t pos = 0;
        int timeMs = -1;
        while (pos < rawLine.size() && rawLine[pos] == L'[') {
            size_t close = rawLine.find(L']', pos);
            if (close == std::wstring::npos) break;

            std::wstring tag = rawLine.substr(pos + 1, close - pos - 1);
            size_t colon = tag.find(L':');
            size_t dot = tag.find(L'.');
            if (colon != std::wstring::npos && dot != std::wstring::npos && colon < dot) {
                int mm = _wtoi(tag.substr(0, colon).c_str());
                int ss = _wtoi(tag.substr(colon + 1, dot - colon - 1).c_str());
                std::wstring frac = tag.substr(dot + 1);
                int fracVal = _wtoi(frac.c_str());
                int fracMs = frac.size() >= 3 ? fracVal : fracVal * 10;
                timeMs = mm * 60000 + ss * 1000 + fracMs;
            }
            pos = close + 1;
        }

        if (timeMs >= 0) {
            std::wstring text = rawLine.substr(pos);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            if (!text.empty()) lines.push_back({timeMs, text});
        }
    }

    std::stable_sort(lines.begin(), lines.end(),
                      [](const Line& a, const Line& b) { return a.timeMs < b.timeMs; });
    return lines;
}

// ---------------------------------------------------------------------------
// Minimal synchronous WinHTTP GET - fine here since this only ever runs
// already off the UI thread (inside winrt::resume_background).
// ---------------------------------------------------------------------------

struct HttpResponse {
    bool ok = false;
    int status = 0;
    std::string body;
};

HttpResponse HttpsGet(const wchar_t* host, const std::wstring& path) {
    HttpResponse resp;

    HINTERNET hSession = WinHttpOpen(L"EdgeDeck/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return resp;

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return resp;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
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
        while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD bytesRead = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), available, &bytesRead)) break;
            chunk.resize(bytesRead);
            resp.body += chunk;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

winrt::fire_and_forget FetchAsync(HWND notifyWindow, UINT notifyMessage) {
    auto* result = new Result();
    auto post = [&] { PostMessageW(notifyWindow, notifyMessage, reinterpret_cast<WPARAM>(result), 0); };

    try {
        co_await winrt::resume_background();

        // Whatever Windows itself currently considers the "now playing" app -
        // same pick MediaWidget originally used for its single-session mode.
        Manager manager = co_await Manager::RequestAsync();
        auto session = manager.GetCurrentSession();
        if (!session) {
            result->status = Status::NothingPlaying;
            post();
            co_return;
        }

        auto mediaProps = co_await session.TryGetMediaPropertiesAsync();
        std::wstring title = mediaProps.Title().c_str();
        std::wstring artist = mediaProps.Artist().c_str();
        int positionMs =
            static_cast<int>(session.GetTimelineProperties().Position().count() / 10000);

        if (title.empty()) {
            result->status = Status::NothingPlaying;
            post();
            co_return;
        }

        std::wstring trackKey = artist.empty() ? title : (artist + L" - " + title);
        result->trackKey = trackKey;
        result->positionMs = positionMs;

        // Cache hit: done, no network call at all.
        if (ReadCache(trackKey, result->lines)) {
            result->status = Status::Success;
            post();
            co_return;
        }

        std::wstring path = L"/api/search?track_name=" + UrlEncode(title);
        if (!artist.empty()) path += L"&artist_name=" + UrlEncode(artist);

        HttpResponse resp = HttpsGet(L"lrclib.net", path);
        if (!resp.ok || resp.status != 200) {
            result->status = Status::NetworkError;
            post();
            co_return;
        }

        JsonArray candidates = JsonArray::Parse(Utf8ToWide(resp.body));
        std::wstring synced;
        for (auto const& candidate : candidates) {
            auto obj = candidate.GetObject();
            if (obj.GetNamedBoolean(L"instrumental", false)) continue;
            std::wstring s = obj.GetNamedString(L"syncedLyrics", L"").c_str();
            if (!s.empty()) {
                synced = s;
                break;
            }
        }
        if (synced.empty()) {
            result->status = Status::NoLyrics;
            post();
            co_return;
        }

        std::vector<Line> lines = ParseLrc(synced);
        if (lines.empty()) {
            result->status = Status::NoLyrics;
            post();
            co_return;
        }

        WriteCache(trackKey, lines);
        result->lines = std::move(lines);
        result->status = Status::Success;
        post();
    } catch (...) {
        result->status = Status::NetworkError;
        post();
    }
}

} // namespace

void FetchCurrentLyrics(HWND notifyWindow, UINT notifyMessage) {
    FetchAsync(notifyWindow, notifyMessage);
}

} // namespace LrcLyrics

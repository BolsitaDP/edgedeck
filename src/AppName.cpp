#include "AppName.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <mutex>

namespace AppName {
namespace {

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

bool EndsWithNoCase(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

// "C:\Apps\Spotify.exe" -> "Spotify".
std::wstring FileStem(const std::wstring& path) {
    std::wstring name = path;
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name.erase(0, slash + 1);
    if (EndsWithNoCase(name, L".exe")) name.resize(name.size() - 4);
    return name;
}

std::wstring LastDotSegment(const std::wstring& s) {
    const size_t dot = s.find_last_of(L'.');
    return dot == std::wstring::npos || dot + 1 == s.size() ? s : s.substr(dot + 1);
}

// The executable's own description of itself ("VLC media player" for vlc.exe), or empty when it
// has none worth showing.
std::wstring FileDescription(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0) return {};
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation* translations = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void**>(&translations), &length) ||
        length < sizeof(Translation)) {
        return {};
    }
    wchar_t key[64];
    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].language,
               translations[0].codePage);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &length) || !value) {
        return {};
    }
    std::wstring description(value);
    while (!description.empty() && iswspace(description.back())) description.pop_back();
    while (!description.empty() && iswspace(description.front())) description.erase(0, 1);
    // A sentence rather than a name ("Spotify, the music app for everyone...") is no label.
    return description.size() <= 40 ? description : std::wstring{};
}

std::wstring ExeName(const std::wstring& path) {
    std::wstring description = FileDescription(path);
    return description.empty() ? FileStem(path) : description;
}

// Every running executable that has a libcef.dll beside it, with its visible windows' titles.
std::vector<Host> FindChromiumHosts() {
    std::vector<Host> hosts;
    std::vector<std::wstring> others; // executables already known not to be hosts
    struct Process {
        DWORD pid;
        size_t host;
    };
    std::vector<Process> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return hosts;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) continue;
        wchar_t image[MAX_PATH];
        DWORD length = MAX_PATH;
        const bool named = QueryFullProcessImageNameW(process, 0, image, &length) != FALSE;
        CloseHandle(process);
        if (!named) continue;

        const std::wstring path(image, length);
        auto known = std::find_if(hosts.begin(), hosts.end(),
                                  [&](const Host& h) { return _wcsicmp(h.exePath.c_str(), path.c_str()) == 0; });
        if (known != hosts.end()) {
            processes.push_back({entry.th32ProcessID, static_cast<size_t>(known - hosts.begin())});
            continue;
        }
        if (std::any_of(others.begin(), others.end(),
                        [&](const std::wstring& p) { return _wcsicmp(p.c_str(), path.c_str()) == 0; })) {
            continue;
        }
        const size_t slash = path.find_last_of(L'\\');
        const std::wstring cef = path.substr(0, slash + 1) + L"libcef.dll";
        if (slash == std::wstring::npos || GetFileAttributesW(cef.c_str()) == INVALID_FILE_ATTRIBUTES) {
            others.push_back(path);
            continue;
        }
        Host host;
        host.exePath = path;
        host.pid = entry.th32ProcessID;
        hosts.push_back(std::move(host));
        processes.push_back({entry.th32ProcessID, hosts.size() - 1});
    }
    CloseHandle(snapshot);
    if (hosts.empty()) return hosts;

    struct Search {
        std::vector<Host>* hosts;
        std::vector<Process>* processes;
        std::vector<bool> windowed;
    } search{&hosts, &processes, std::vector<bool>(hosts.size(), false)};
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(param);
            if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            auto process = std::find_if(s.processes->begin(), s.processes->end(),
                                        [&](const Process& p) { return p.pid == pid; });
            if (process == s.processes->end()) return TRUE;
            Host& host = (*s.hosts)[process->host];
            // InternalGetWindowText, not GetWindowText: it reads the title Windows keeps instead
            // of sending a message to the window, so a hung app cannot stall this thread.
            wchar_t title[256];
            const int length = InternalGetWindowText(window, title, 256);
            if (length > 0) host.windowTitles.emplace_back(title, static_cast<size_t>(length));
            if (!s.windowed[process->host]) {
                s.windowed[process->host] = true;
                host.pid = pid;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    for (Host& host : hosts) host.name = ExeName(host.exePath);
    return hosts;
}

bool Running(unsigned long pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}

} // namespace

Parsed ParseAumid(const std::wstring& aumid) {
    Parsed parsed;
    if (aumid.empty()) return parsed;

    // A classic app, by its executable: "C:\...\vlc.exe", or just "Spotify.exe".
    if (aumid.find_first_of(L"\\/") != std::wstring::npos) {
        if (EndsWithNoCase(aumid, L".exe")) parsed.exePath = aumid;
        parsed.name = FileStem(aumid);
        return parsed;
    }
    if (EndsWithNoCase(aumid, L".exe")) {
        parsed.name = FileStem(aumid);
        return parsed;
    }

    // The Chromium family: the browsers name themselves, with a per-user or per-site-app suffix
    // ("Chrome._crx_..." is a site installed as an app, shown under its browser); unbranded
    // Chromium is whichever app embeds it.
    if (_wcsicmp(aumid.c_str(), L"Chromium") == 0 || StartsWithNoCase(aumid, L"Chromium.")) {
        parsed.name = L"Chromium";
        parsed.chromium = true;
        return parsed;
    }
    if (_wcsicmp(aumid.c_str(), L"Chrome") == 0 || StartsWithNoCase(aumid, L"Chrome.")) {
        parsed.name = L"Google Chrome";
        return parsed;
    }
    if (_wcsicmp(aumid.c_str(), L"MSEdge") == 0 || StartsWithNoCase(aumid, L"MSEdge.")) {
        parsed.name = L"Microsoft Edge";
        return parsed;
    }

    // A packaged app: "Publisher.Package_publisherid!AppId". The app id is usually the better name,
    // unless it is the generic "App" the project templates give.
    const size_t bang = aumid.find(L'!');
    if (bang != std::wstring::npos) {
        std::wstring app = aumid.substr(bang + 1);
        if (app.empty() || _wcsicmp(app.c_str(), L"App") == 0 || StartsWithNoCase(app, L"App.")) {
            app = aumid.substr(0, std::min(bang, aumid.find(L'_')));
        }
        parsed.name = LastDotSegment(app);
        return parsed;
    }

    parsed.name = aumid;
    return parsed;
}

int PickChromiumHost(const std::vector<Host>& hosts, const std::wstring& trackTitle) {
    if (!trackTitle.empty()) {
        int showing = -1;
        int count = 0;
        for (size_t i = 0; i < hosts.size(); ++i) {
            const auto& titles = hosts[i].windowTitles;
            if (std::any_of(titles.begin(), titles.end(),
                            [&](const std::wstring& t) { return t.find(trackTitle) != std::wstring::npos; })) {
                showing = static_cast<int>(i);
                ++count;
            }
        }
        if (count == 1) return showing;
    }

    std::vector<int> apps;
    for (size_t i = 0; i < hosts.size(); ++i) {
        if (Lower(FileStem(hosts[i].exePath)).find(L"helper") == std::wstring::npos) {
            apps.push_back(static_cast<int>(i));
        }
    }
    if (apps.size() == 1) return apps.front();
    return -1;
}

std::wstring Resolve(const std::wstring& aumid, const std::wstring& trackTitle) {
    struct Entry {
        std::wstring aumid;
        std::wstring name;
        unsigned long pid; // the app the name was found in, for the Chromium id; 0 otherwise
    };
    static std::mutex mutex;
    static std::vector<Entry> cache;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto found = std::find_if(cache.begin(), cache.end(), [&](const Entry& e) { return e.aumid == aumid; });
        if (found != cache.end()) {
            // The Chromium id is shared by every CEF app: once the one it was found in has quit,
            // the next one to play may be another.
            if (found->pid == 0 || Running(found->pid)) return found->name;
            cache.erase(found);
        }
    }

    Parsed parsed = ParseAumid(aumid);
    std::wstring name = parsed.name;
    unsigned long pid = 0;
    if (!parsed.exePath.empty()) {
        name = ExeName(parsed.exePath);
    } else if (parsed.chromium) {
        const std::vector<Host> hosts = FindChromiumHosts();
        const int pick = PickChromiumHost(hosts, trackTitle);
        // Not remembered when it cannot be told: the next refresh may have the track's title.
        if (pick < 0) return name;
        name = hosts[static_cast<size_t>(pick)].name;
        pid = hosts[static_cast<size_t>(pick)].pid;
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (cache.size() >= 32) cache.erase(cache.begin());
    cache.push_back({aumid, name, pid});
    return name;
}

} // namespace AppName

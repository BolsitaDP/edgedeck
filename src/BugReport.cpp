#include "BugReport.h"

#include "Diagnostics.h"

#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cwctype>

namespace BugReport {

namespace {

std::wstring ReadRegistryString(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t buffer[128] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return L"";
    return buffer;
}

// "Windows 11 24H2 (build 26100.2605)". ProductName is no help: it still says "Windows 10" on 11,
// so the number comes from the build, as Microsoft's own tools do it.
std::wstring DescribeWindows() {
    const wchar_t* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const std::wstring build = ReadRegistryString(HKEY_LOCAL_MACHINE, key, L"CurrentBuildNumber");
    const std::wstring release = ReadRegistryString(HKEY_LOCAL_MACHINE, key, L"DisplayVersion");
    DWORD ubr = 0;
    DWORD size = sizeof(ubr);
    const bool hasUbr =
        RegGetValueW(HKEY_LOCAL_MACHINE, key, L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &size) == ERROR_SUCCESS;
    if (build.empty()) return L"Windows (unknown version)";

    std::wstring text = _wtoi(build.c_str()) >= 22000 ? L"Windows 11" : L"Windows 10";
    if (!release.empty()) text += L" " + release;
    text += L" (build " + build;
    if (hasUbr) text += L"." + std::to_wstring(ubr);
    return text + L")";
}

std::wstring MachineName(USHORT machine) {
    switch (machine) {
        case IMAGE_FILE_MACHINE_AMD64: return L"x64";
        case IMAGE_FILE_MACHINE_ARM64: return L"ARM64";
        case IMAGE_FILE_MACHINE_I386: return L"x86";
        default: return L"machine " + std::to_wstring(machine);
    }
}

// The build's own architecture, and the machine's when they differ: an x64 build on an ARM64 PC
// runs emulated, which is worth knowing in a report.
std::wstring DescribeArchitecture() {
#if defined(_M_ARM64)
    const std::wstring built = L"ARM64";
#else
    const std::wstring built = L"x64";
#endif
    USHORT process = 0;
    USHORT native = 0;
    if (!IsWow64Process2(GetCurrentProcess(), &process, &native)) return built;
    const std::wstring machine = MachineName(native);
    return machine == built ? built : built + L", emulated on " + machine;
}

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || dpiX == 0) dpiX = 96;
    Monitor m;
    m.width = info.rcMonitor.right - info.rcMonitor.left;
    m.height = info.rcMonitor.bottom - info.rcMonitor.top;
    m.scalePercent = static_cast<int>((dpiX * 100 + 48) / 96);
    m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    reinterpret_cast<std::vector<Monitor>*>(data)->push_back(m);
    return TRUE;
}

// The end of the log: the last 32 KB is far more than kLogLines lines of it. The log may be open
// for writing, so it is shared both ways.
std::wstring ReadLogTail(const std::wstring& path) {
    if (path.empty()) return L"";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    constexpr LONGLONG kTail = 32 * 1024;
    const bool cut = size.QuadPart > kTail;
    LARGE_INTEGER start{};
    start.QuadPart = cut ? size.QuadPart - kTail : 0;
    std::string bytes(static_cast<size_t>(cut ? kTail : size.QuadPart), '\0');
    DWORD read = 0;
    if (!bytes.empty() && SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) {
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    }
    CloseHandle(file);
    bytes.resize(read);

    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(static_cast<size_t>(length > 0 ? length : 0), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    }
    return LastLines(text, kLogLines, cut);
}

std::wstring NewestDump(const std::wstring& logPath) {
    if (logPath.empty()) return L"";
    const std::wstring folder = logPath.substr(0, logPath.find_last_of(L'\\')) + L"\\crashes";
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((folder + L"\\*.dmp").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return L"";
    std::wstring newest;
    FILETIME newestTime{};
    do {
        if (newest.empty() || CompareFileTime(&found.ftLastWriteTime, &newestTime) > 0) {
            newest = found.cFileName;
            newestTime = found.ftLastWriteTime;
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return newest;
}

} // namespace

Facts Gather(const std::wstring& version) {
    Facts facts;
    facts.version = version;
    facts.windows = DescribeWindows();
    facts.architecture = DescribeArchitecture();
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&facts.monitors));
    const std::wstring log = Diagnostics::LogPath();
    facts.log = ReadLogTail(log);
    facts.newestDump = NewestDump(log);
    wchar_t profile[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length > 0 && length < MAX_PATH) facts.userProfile = profile;
    return facts;
}

std::wstring DescribeMonitors(const std::vector<Monitor>& monitors) {
    std::wstring text;
    for (const Monitor& m : monitors) {
        if (!text.empty()) text += L"\n";
        text += std::to_wstring(m.width) + L"x" + std::to_wstring(m.height) + L" at " +
                std::to_wstring(m.scalePercent) + L"%";
        if (m.primary) text += L" (primary)";
    }
    return text;
}

std::wstring ClipboardText(const Facts& facts) {
    std::wstring text = Mask(facts.log, facts.userProfile, L"%USERPROFILE%");
    if (!facts.newestDump.empty()) {
        if (!text.empty() && text.back() != L'\n') text += L"\n";
        text += L"\nNewest crash dump: %LOCALAPPDATA%\\EdgeDeck\\crashes\\" +
                Mask(facts.newestDump, facts.userProfile, L"%USERPROFILE%") + L"\n";
    }
    return text;
}

std::wstring IssueUrl(std::wstring_view homepage, const Facts& facts) {
    std::wstring url(homepage);
    url += L"/issues/new?template=bug_report.yml";
    url += L"&version=" + EncodeQueryValue(facts.version + L" (" + facts.architecture + L")");
    url += L"&windows=" + EncodeQueryValue(facts.windows);
    if (!facts.monitors.empty()) url += L"&displays=" + EncodeQueryValue(DescribeMonitors(facts.monitors));
    return url;
}

std::wstring LastLines(std::wstring_view text, size_t count, bool cut) {
    // Lines end in "\n" or "\r\n"; a trailing newline does not start an empty last line.
    size_t end = text.size();
    while (end > 0 && (text[end - 1] == L'\n' || text[end - 1] == L'\r')) --end;
    text = text.substr(0, end);
    if (cut) {
        // Read from the middle of a file, the first line is most likely the end of one.
        const size_t first = text.find(L'\n');
        text = first == std::wstring_view::npos ? std::wstring_view{} : text.substr(first + 1);
    }
    size_t begin = text.size();
    for (size_t lines = 0; lines < count && begin > 0; ++lines) {
        const size_t newline = text.rfind(L'\n', begin - 1);
        begin = newline == std::wstring_view::npos ? 0 : newline; // the newline before this line
    }
    if (begin < text.size() && text[begin] == L'\n') ++begin;
    std::wstring result(text.substr(begin));
    if (!result.empty()) result += L"\n";
    return result;
}

std::wstring Mask(std::wstring text, std::wstring_view secret, std::wstring_view replacement) {
    if (secret.empty() || text.size() < secret.size()) return text;
    auto lower = [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); };
    size_t at = 0;
    while (at + secret.size() <= text.size()) {
        bool same = true;
        for (size_t i = 0; i < secret.size() && same; ++i) same = lower(text[at + i]) == lower(secret[i]);
        if (same) {
            text.replace(at, secret.size(), replacement);
            at += replacement.size();
        } else {
            ++at;
        }
    }
    return text;
}

std::wstring EncodeQueryValue(std::wstring_view value) {
    std::string utf8;
    if (!value.empty()) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                               nullptr, nullptr);
        utf8.assign(static_cast<size_t>(length > 0 ? length : 0), '\0');
        if (length > 0) {
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), length, nullptr,
                                nullptr);
        }
    }
    static const wchar_t* kHex = L"0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : utf8) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '_' || c == '.' || c == '~';
        if (plain) {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            out.push_back(L'%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

} // namespace BugReport

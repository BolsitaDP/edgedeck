#include "RecentFiles.h"

#include "ActionIcons.h"
#include "Diagnostics.h"
#include "Strings.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <thread>

namespace RecentFiles {
namespace {

std::wstring Lower(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

std::wstring ExtensionOf(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= name.size()) return {};
    return name.substr(dot + 1);
}

std::uint64_t Ticks(const FILETIME& t) {
    return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

// The icon of a file that has one of its own, and nothing for a document: that picture is the
// shell's to give, and asking costs the process far more than a row is worth.
bool CarriesItsOwnIcon(const std::wstring& name) {
    const std::wstring extension = Lower(ExtensionOf(name));
    return extension == L"exe" || extension == L"lnk" || extension == L"ico" || extension == L"scr";
}

} // namespace

std::wstring DefaultFolder() {
    // The Downloads folder is a "known folder", and where it lives is this value of the user's
    // shell-folders key (the data is usually %USERPROFILE%\Downloads, and is unexpanded text).
    wchar_t raw[MAX_PATH * 2] = {};
    DWORD size = sizeof(raw) - sizeof(wchar_t);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
        L"{374DE290-123F-4565-9164-39C4925E467B}", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
        nullptr, raw, &size);
    if (status == ERROR_SUCCESS && raw[0] != L'\0') {
        wchar_t expanded[MAX_PATH * 2] = {};
        const DWORD length = ExpandEnvironmentStringsW(raw, expanded, MAX_PATH * 2);
        if (length > 0 && length <= MAX_PATH * 2) return expanded;
    }

    wchar_t profile[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return std::wstring(profile, length) + L"\\Downloads";
    return {};
}

std::wstring SanitizeFolder(std::wstring text) {
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](wchar_t c) { return c == L'\r' || c == L'\n'; }),
               text.end());
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t");
    text = text.substr(first, last - first + 1);
    if (text.size() > kMaxFolderLength) text.resize(kMaxFolderLength);
    return text;
}

bool IsPartialDownload(const std::wstring& name) {
    if (name.size() >= 2 && name[0] == L'~' && name[1] == L'$') return true; // Office's lock file
    const std::wstring extension = Lower(ExtensionOf(name));
    return extension == L"crdownload" || extension == L"part" || extension == L"partial" ||
           extension == L"download" || extension == L"opdownload" || extension == L"tmp";
}

Listing Newest(const std::wstring& folder, std::size_t count) {
    Listing listing;
    if (folder.empty() || count == 0) return listing;

    std::wstring directory = folder;
    while (directory.size() > 1 && (directory.back() == L'\\' || directory.back() == L'/')) {
        directory.pop_back();
    }

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW((directory + L"\\*").c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        // Not there (or not readable): the caller says so. An empty folder is a different answer
        // and gets past this point, because FindFirstFile finds "." in it.
        return listing;
    }
    listing.folderFound = true;

    std::size_t scanned = 0;
    do {
        if (++scanned > kMaxScanned) break;
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
            continue;
        }
        const std::wstring name = data.cFileName;
        if (IsPartialDownload(name)) continue;

        // Keep only the newest `count` seen so far: a folder of thousands of files is read in one
        // pass and never held.
        const std::uint64_t written = Ticks(data.ftLastWriteTime);
        std::vector<Entry>& best = listing.newest;
        if (best.size() == count && written <= Ticks(best.back().written)) continue;

        Entry entry;
        entry.name = name;
        entry.path = directory + L"\\" + name;
        entry.written = data.ftLastWriteTime;
        const auto place = std::find_if(best.begin(), best.end(), [&](const Entry& e) {
            return written > Ticks(e.written);
        });
        best.insert(place, std::move(entry));
        if (best.size() > count) best.pop_back();
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return listing;
}

std::wstring ExtensionBadge(const std::wstring& name) {
    std::wstring extension = ExtensionOf(name);
    if (extension.size() > 3) extension.resize(3);
    for (wchar_t& c : extension) c = static_cast<wchar_t>(towupper(c));
    return extension;
}

std::wstring DescribeAge(long long seconds) {
    using Strings::Id;
    if (seconds < 60) return TR(AgeJustNow); // includes a file stamped slightly in the future
    if (seconds < 3600) return Strings::Format(Id::AgeMinutes, seconds / 60);
    if (seconds < 86400) return Strings::Format(Id::AgeHours, seconds / 3600);
    if (seconds < 2 * 86400) return TR(AgeYesterday);
    if (seconds < 7 * 86400) return Strings::Format(Id::AgeDays, seconds / 86400);
    return {};
}

void LoadAsync(std::wstring folder, std::size_t count, HWND notifyWindow, UINT notifyMessage,
               std::uint64_t requestId) {
    auto* batch = new RecentBatch();
    batch->requestId = requestId;
    batch->folder = std::move(folder);

    try {
        std::thread([batch, count, notifyWindow, notifyMessage] {
            try {
                batch->listing = Newest(batch->folder, count);
                batch->images.resize(batch->listing.newest.size());
                for (size_t i = 0; i < batch->listing.newest.size(); ++i) {
                    const Entry& entry = batch->listing.newest[i];
                    if (CarriesItsOwnIcon(entry.name)) {
                        batch->images[i] = ActionIcons::ImageForTarget(entry.path);
                    }
                }
                if (batch->listing.folderFound) {
                    batch->folderImage = ActionIcons::ImageForTarget(batch->folder);
                }
            } catch (...) {
                // A listing that threw is "nothing found": the panel says so instead of hanging.
            }
            PostOrDelete(notifyWindow, notifyMessage, batch);
        }).detach();
    } catch (...) {
        Diagnostics::Error("RecentFiles: could not start the listing thread");
        PostOrDelete(notifyWindow, notifyMessage, batch);
    }
}

} // namespace RecentFiles

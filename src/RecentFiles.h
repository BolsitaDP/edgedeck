#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "AsyncResult.h"
#include "PanelImage.h"

// The newest files in a folder (Downloads, by default), for the Recent files tab. Listed when the
// panel opens and not watched: no change notification, no timer, so a closed panel costs nothing
// and an open one costs one directory listing, on a short-lived thread.
namespace RecentFiles {

// How many files a panel shows. A panel is a glance, not a file manager.
inline constexpr std::size_t kRows = 8;
// A folder with more entries than this is not read to the end: the listing is of what was seen.
inline constexpr std::size_t kMaxScanned = 50000;
inline constexpr std::size_t kMaxFolderLength = 520;

struct Entry {
    std::wstring name; // the file's name, with its extension
    std::wstring path; // the full path, which is what opening it needs
    FILETIME written{};
};

struct Listing {
    bool folderFound = false;
    std::vector<Entry> newest; // newest first, at most the count asked for
};

// The user's Downloads folder, read from the registry (the shell would say the same for 8 MB of
// process); %USERPROFILE%\Downloads when that cannot be read.
std::wstring DefaultFolder();

// A folder setting as it will be kept: whitespace trimmed, line breaks removed (the file is one
// value per line), capped. Empty stays empty and means "the default".
std::wstring SanitizeFolder(std::wstring text);

// What the listing leaves out: a download that is not finished (.crdownload, .part, ...), Office's
// "~$" lock files, and hidden or system files. Folders are not files and are left out as well.
bool IsPartialDownload(const std::wstring& name);

// The newest `count` files of `folder`, newest first. Reads the folder once.
Listing Newest(const std::wstring& folder, std::size_t count);

// The extension as a badge: "pdf" -> "PDF", at most three letters ("docx" -> "DOC"). Empty for a
// name with no extension.
std::wstring ExtensionBadge(const std::wstring& name);

// "just now", "5 min ago", "3 h ago", "yesterday", "4 days ago"; empty for anything a week old or
// more, which the caller says as a date.
std::wstring DescribeAge(long long seconds);

// The answer to LoadAsync: the listing, a picture for each file that carries its own icon (a
// program, a shortcut), and the folder's picture for the "open folder" row.
struct RecentBatch : AsyncEnvelope {
    AsyncKind Kind() const override { return AsyncKind::RecentFiles; }
    std::wstring folder;
    Listing listing;
    std::vector<std::shared_ptr<const PanelImage>> images; // parallel to listing.newest
    std::shared_ptr<const PanelImage> folderImage;
};

// Lists the folder on a thread of its own and posts a RecentBatch to notifyWindow as
// notifyMessage, stamped with requestId. Ownership follows PostOrDelete.
void LoadAsync(std::wstring folder, std::size_t count, HWND notifyWindow, UINT notifyMessage,
               std::uint64_t requestId);

} // namespace RecentFiles

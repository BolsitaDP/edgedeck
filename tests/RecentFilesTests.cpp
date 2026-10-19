// The Recent files tab's logic: which files a folder listing keeps and in what order, what it
// leaves out, how long ago is said, and the folder setting. How it looks on screen and what a click
// does is tools/scripts/Test-RecentFiles.ps1.
#include "TestHarness.h"

#include "Config.h"
#include "RecentFiles.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

// A folder of its own under the (scratch) temp directory, emptied again at the end.
class ScratchFolder {
public:
    ScratchFolder() {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        m_path = std::wstring(temp) + L"edgedeck-recent-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(m_path.c_str(), nullptr);
    }
    ~ScratchFolder() {
        // Files first, then the one level of folder made here.
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW((m_path + L"\\*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..") continue;
                const std::wstring full = m_path + L"\\" + name;
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(full.c_str());
                else DeleteFileW(full.c_str());
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
        RemoveDirectoryW(m_path.c_str());
    }
    const std::wstring& Path() const { return m_path; }

    // A file written `minutesAgo` minutes in the past.
    void File(const std::wstring& name, int minutesAgo, DWORD attributes = FILE_ATTRIBUTE_NORMAL) const {
        const std::wstring full = m_path + L"\\" + name;
        HANDLE h = CreateFileW(full.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER t{};
        t.LowPart = now.dwLowDateTime;
        t.HighPart = now.dwHighDateTime;
        t.QuadPart -= static_cast<ULONGLONG>(minutesAgo) * 60ULL * 10000000ULL;
        FILETIME then{};
        then.dwLowDateTime = t.LowPart;
        then.dwHighDateTime = t.HighPart;
        SetFileTime(h, nullptr, nullptr, &then);
        CloseHandle(h);
        if (attributes != FILE_ATTRIBUTE_NORMAL) SetFileAttributesW(full.c_str(), attributes);
    }
    void Folder(const std::wstring& name) const { CreateDirectoryW((m_path + L"\\" + name).c_str(), nullptr); }

private:
    std::wstring m_path;
};

void TestNewestFilesComeFirstAndOnlyTheNewest() {
    TEST("RecentFiles: the newest files, newest first, no more than asked for");
    ScratchFolder folder;
    folder.File(L"oldest.txt", 500);
    folder.File(L"middle.pdf", 60);
    folder.File(L"newest.zip", 1);
    folder.File(L"second.png", 10);

    const RecentFiles::Listing all = RecentFiles::Newest(folder.Path(), 8);
    CHECK(all.folderFound);
    CHECK_EQ(all.newest.size(), size_t{4});
    if (all.newest.size() == 4) {
        CHECK(all.newest[0].name == L"newest.zip");
        CHECK(all.newest[1].name == L"second.png");
        CHECK(all.newest[2].name == L"middle.pdf");
        CHECK(all.newest[3].name == L"oldest.txt");
        CHECK(all.newest[0].path == folder.Path() + L"\\newest.zip");
    }

    const RecentFiles::Listing two = RecentFiles::Newest(folder.Path(), 2);
    CHECK_EQ(two.newest.size(), size_t{2});
    if (two.newest.size() == 2) {
        CHECK(two.newest[0].name == L"newest.zip");
        CHECK(two.newest[1].name == L"second.png");
    }

    // A trailing slash is the same folder.
    CHECK_EQ(RecentFiles::Newest(folder.Path() + L"\\", 8).newest.size(), size_t{4});
}

void TestWhatTheListingLeavesOut() {
    TEST("RecentFiles: folders, hidden and system files, and unfinished downloads are not listed");
    ScratchFolder folder;
    folder.File(L"keep.docx", 30);
    folder.Folder(L"a folder");
    folder.File(L"hidden.txt", 1, FILE_ATTRIBUTE_HIDDEN);
    folder.File(L"system.dat", 1, FILE_ATTRIBUTE_SYSTEM);
    folder.File(L"video.mp4.crdownload", 1);
    folder.File(L"archive.zip.part", 1);
    folder.File(L"~$report.docx", 1);
    folder.File(L"scratch.tmp", 1);

    const RecentFiles::Listing listing = RecentFiles::Newest(folder.Path(), 8);
    CHECK(listing.folderFound);
    CHECK_EQ(listing.newest.size(), size_t{1});
    if (!listing.newest.empty()) CHECK(listing.newest[0].name == L"keep.docx");

    CHECK(RecentFiles::IsPartialDownload(L"x.CRDOWNLOAD"));
    CHECK(RecentFiles::IsPartialDownload(L"~$a.xlsx"));
    CHECK(!RecentFiles::IsPartialDownload(L"photo.png"));
    CHECK(!RecentFiles::IsPartialDownload(L"download"));
}

void TestAMissingFolderIsNotAnEmptyOne() {
    TEST("RecentFiles: a folder that is not there is reported as such, and an empty one is found");
    ScratchFolder folder;
    CHECK(!RecentFiles::Newest(folder.Path() + L"\\no-such-subfolder", 8).folderFound);
    CHECK(!RecentFiles::Newest(L"", 8).folderFound);
    CHECK(!RecentFiles::Newest(L"Z:\\edgedeck\\no\\such\\drive", 8).folderFound);

    const RecentFiles::Listing empty = RecentFiles::Newest(folder.Path(), 8);
    CHECK(empty.folderFound);
    CHECK(empty.newest.empty());
}

void TestAgesAreSaidInWords() {
    TEST("RecentFiles: how long ago is said in words, and left to a date after a week");
    CHECK(RecentFiles::DescribeAge(0) == L"just now");
    CHECK(RecentFiles::DescribeAge(-30) == L"just now"); // a clock a little ahead
    CHECK(RecentFiles::DescribeAge(59) == L"just now");
    CHECK(RecentFiles::DescribeAge(60) == L"1 min ago");
    CHECK(RecentFiles::DescribeAge(59 * 60) == L"59 min ago");
    CHECK(RecentFiles::DescribeAge(3600) == L"1 h ago");
    CHECK(RecentFiles::DescribeAge(23 * 3600 + 59) == L"23 h ago");
    CHECK(RecentFiles::DescribeAge(86400) == L"yesterday");
    CHECK(RecentFiles::DescribeAge(2 * 86400 - 1) == L"yesterday");
    CHECK(RecentFiles::DescribeAge(2 * 86400) == L"2 days ago");
    CHECK(RecentFiles::DescribeAge(6 * 86400) == L"6 days ago");
    CHECK(RecentFiles::DescribeAge(7 * 86400).empty());
}

void TestTheBadgeIsTheExtension() {
    TEST("RecentFiles: a file without an icon of its own is badged with its extension");
    CHECK(RecentFiles::ExtensionBadge(L"report.pdf") == L"PDF");
    CHECK(RecentFiles::ExtensionBadge(L"notes.docx") == L"DOC"); // three letters at most
    CHECK(RecentFiles::ExtensionBadge(L"archive.tar.gz") == L"GZ");
    CHECK(RecentFiles::ExtensionBadge(L"README").empty());
    CHECK(RecentFiles::ExtensionBadge(L".gitignore").empty()); // a name, not an extension
    CHECK(RecentFiles::ExtensionBadge(L"trailing.").empty());
}

void TestTheFolderSettingIsKeptAndCleaned() {
    TEST("RecentFiles: the folder setting is cleaned, saved with the settings and read back");
    CHECK(RecentFiles::SanitizeFolder(L"  C:\\Users\\me\\Pictures \t") == L"C:\\Users\\me\\Pictures");
    CHECK(RecentFiles::SanitizeFolder(L"C:\\a\r\nb") == L"C:\\ab");
    CHECK(RecentFiles::SanitizeFolder(L"   ").empty());
    CHECK_EQ(RecentFiles::SanitizeFolder(std::wstring(900, L'x')).size(), RecentFiles::kMaxFolderLength);

    // The default is somewhere real: the registry's, or the profile's Downloads.
    const std::wstring fallback = RecentFiles::DefaultFolder();
    CHECK(!fallback.empty());
    CHECK(fallback.find(L"%") == std::wstring::npos); // expanded

    Config::SetRecentFolder(L"  D:\\Shots  ");
    CHECK(Config::RecentFolder() == L"D:\\Shots");
    std::vector<TabSettings> tabs = {{WidgetType::RecentFiles, 0.5f, 26.0f, 76.0f, 320.0f}};
    CHECK(Config::Save(tabs));
    Config::SetRecentFolder(L"");
    const auto loaded = Config::LoadOrDefault();
    CHECK(Config::RecentFolder() == L"D:\\Shots");
    CHECK_EQ(loaded.size(), size_t{1});
    if (loaded.size() == 1) CHECK(loaded[0].widgetType == WidgetType::RecentFiles);

    // An unset folder is not written, so a file that never had one stays as it was.
    Config::SetRecentFolder(L"");
    CHECK(Config::Save(tabs));
    Config::SetRecentFolder(L"stale");
    Config::LoadOrDefault();
    CHECK(Config::RecentFolder().empty()); // and a stale value does not linger over a file without
}

} // namespace

void RunRecentFilesTests() {
    TestNewestFilesComeFirstAndOnlyTheNewest();
    TestWhatTheListingLeavesOut();
    TestAMissingFolderIsNotAnEmptyOne();
    TestAgesAreSaidInWords();
    TestTheBadgeIsTheExtension();
    TestTheFolderSettingIsKeptAndCleaned();
}

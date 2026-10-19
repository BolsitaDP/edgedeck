#pragma once

#include <string>
#include <string_view>
#include <vector>

// "Report a problem...": what a bug report needs, gathered for the user instead of asked of them.
// The new-issue page opens with the version, Windows and the monitors already filled in (GitHub's
// issue forms take a field's value from the address), and the end of the log - too long for an
// address - goes on the clipboard to be pasted into the Log box. Nothing is sent anywhere: the
// user sees all of it in the form before posting.
//
// The log and the dump name are passed through Mask, so the user's profile folder (which has
// their account name in it) reads %USERPROFILE%.
namespace BugReport {

struct Monitor {
    int width = 0;
    int height = 0;
    int scalePercent = 100;
    bool primary = false;
};

struct Facts {
    std::wstring version;      // "1.0.0"
    std::wstring windows;      // "Windows 11 24H2 (build 26100.2605)"
    std::wstring architecture; // "x64", "ARM64", "x64, emulated on ARM64"
    std::vector<Monitor> monitors;
    std::wstring newestDump;   // the newest crash dump's file name, or empty
    std::wstring log;          // the end of the log, as read
    std::wstring userProfile;  // %USERPROFILE%, masked wherever it appears
};

inline constexpr size_t kLogLines = 60;

// What the system says now. Reads the registry, the monitors, the log and the crash folder; takes
// no time worth mentioning and holds nothing afterwards.
Facts Gather(const std::wstring& version);

// One line per monitor: "3440x1440 at 100% (primary)".
std::wstring DescribeMonitors(const std::vector<Monitor>& monitors);

// The text for the clipboard: the masked end of the log, and the newest dump to attach, if any.
std::wstring ClipboardText(const Facts& facts);

// The new-issue address under `homepage`, with the bug template's fields filled in.
std::wstring IssueUrl(std::wstring_view homepage, const Facts& facts);

// The last `count` lines of `text` (fewer if it has fewer), without a partial first line when the
// text was cut from the middle of a file (`cut`).
std::wstring LastLines(std::wstring_view text, size_t count, bool cut);

// `text` with every occurrence of `secret` - compared without case, as Windows paths are - replaced
// by `replacement`. An empty secret changes nothing.
std::wstring Mask(std::wstring text, std::wstring_view secret, std::wstring_view replacement);

// UTF-8, percent-encoded for a query string value.
std::wstring EncodeQueryValue(std::wstring_view value);

} // namespace BugReport

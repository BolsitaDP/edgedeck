#pragma once

#include <string>
#include <vector>

// The name to show for a media session's source app, from what Windows gives for it: an AUMID.
//
// The ways Windows has of turning any AUMID into its Start menu name (the shell's AppsFolder, or
// WinRT's AppInfo) cost EdgeDeck 8-13 MB of working set for a label, so they are not used. What an
// AUMID says on its own covers most apps; the one common id that says nothing is the one every
// unbranded Chromium build shares - CEF apps such as Spotify report "Chromium.<hash of the user>" -
// and for that one the running processes are looked at instead.
namespace AppName {

// What the AUMID itself gives. `exePath` is set for a classic app identified by its executable
// (its file description is a better name than the file's), and `chromium` for the unbranded
// Chromium id, whose app has to be found among the running processes.
struct Parsed {
    std::wstring name;
    std::wstring exePath;
    bool chromium = false;
};
Parsed ParseAumid(const std::wstring& aumid);

// A running program with Chromium embedded (a libcef.dll beside its executable). One entry per
// executable, however many processes run it.
struct Host {
    std::wstring exePath;
    std::wstring name;                      // its file description, or the file's name
    std::vector<std::wstring> windowTitles; // of its visible top-level windows
    unsigned long pid = 0;                  // one that has a window, if any does
};

// Which of the hosts is the one playing `trackTitle`, or -1 when that cannot be told: the one
// whose window shows the track (Spotify puts it in its title), else the only one that is not a
// helper process (Steam's steamwebhelper.exe also carries libcef.dll).
int PickChromiumHost(const std::vector<Host>& hosts, const std::wstring& trackTitle);

// The name for a session, looking at the system where the id alone is not enough. Remembered per
// AUMID, so the process list is read once per app rather than on every refresh. Safe to call from
// any thread.
std::wstring Resolve(const std::wstring& aumid, const std::wstring& trackTitle);

} // namespace AppName

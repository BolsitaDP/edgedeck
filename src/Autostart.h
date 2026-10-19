#pragma once

#include <string>

// Start-with-Windows via the per-user Run key (HKCU, no admin needed).
// EdgeDeck is a GUI-subsystem exe, so a login launch opens no console window.
//
// "Enabled" here means "will actually start at sign-in", which is more than the
// Run entry existing. Task Manager's Startup tab does not delete an entry when
// the user switches it off; it writes a flag under StartupApproved\Run and leaves
// the entry where it is. Checking only the entry made Settings claim autostart
// was on while Windows quietly skipped it.
namespace Autostart {

// Where the entry lives, as a value so the unit tests can run the same code
// against a scratch key instead of the user's real startup entries.
struct Locations {
    const wchar_t* runKey;      // HKCU subkey holding the command line
    const wchar_t* approvedKey; // HKCU subkey holding Task Manager's on/off flag
    const wchar_t* valueName;
};
Locations Default();

// There is an entry, and Task Manager has not switched it off.
bool IsEnabled(const Locations& where = Default());

// Turns it on or off and reports whether the result reads back as asked. Enabling
// writes the command line and also clears a Task Manager "off" flag, because
// ticking the box in Settings has to actually work for someone who once disabled
// it there. exePath empty means PreferredPath(). Disabling removes the entry and
// the flag.
bool SetEnabled(bool enabled, const Locations& where = Default(), const std::wstring& exePath = {});

enum class Repair {
    NotEnabled, // no entry, so nothing to repair - and nothing is created
    Valid,      // the entry points at a file that exists
    Repointed,  // it pointed at nothing and now points at a copy that exists
    Failed,     // it pointed at nothing and could not be rewritten
};

// Fixes an entry whose file has gone - the exe was moved, or a build folder was
// cleaned - which would otherwise fail silently at every sign-in. An entry that
// points at a *different* EdgeDeck that still exists is left alone: that is how a
// development copy coexists with an installed one, and a copy launched for a test
// must not take the entry over. Never touches Task Manager's flag: whether the
// user wants it on is their decision, and only the path is being repaired.
Repair RepairPath(const Locations& where, const std::wstring& runningExe,
                  const std::wstring& installedExe);

// What the startup entry looks like right now. Finer than IsEnabled, for the one question
// "should the user be told something?".
enum class State {
    NotConfigured,         // no entry at all
    Enabled,               // an entry, its file exists, Task Manager has not switched it off
    DisabledInTaskManager, // an entry the user switched off there: their decision, not a fault
    PointsToMissingFile,   // an entry whose file is gone: it fails at every sign-in, silently
};
State Inspect(const Locations& where = Default());

// What, if anything, to say at startup. Pure, so the rules - which are about not being annoying -
// can be tested without a registry or a tray icon.
struct StartupNote {
    std::wstring text;       // empty: say nothing
    bool settlesHint = false; // the one-time hint was given; remember it so it is not given again
};
// before:           the state found at startup, before any repair
// repair:           what RepairPath did about it
// hintAlreadyShown: the one-time hint has been given on an earlier run
// installedCopy:    this process is the installed copy, not a build folder or a test copy
StartupNote NoteForStartup(State before, Repair repair, bool hintAlreadyShown, bool installedCopy);

// The path inside a Run command line: quoted or not, with or without arguments.
std::wstring CommandToPath(const std::wstring& command);

// Where scripts/install.ps1 puts the app: %LOCALAPPDATA%\EdgeDeck\bin\EdgeDeck.exe.
// Empty if %LOCALAPPDATA% is unusable.
std::wstring InstalledPath();

// This process's own executable.
std::wstring ThisExePath();

// What an entry should point at: the installed copy when there is one, since its
// path never changes when the project is rebuilt; otherwise the running exe.
std::wstring PreferredPath(const std::wstring& installedExe, const std::wstring& runningExe);
inline std::wstring PreferredPath() { return PreferredPath(InstalledPath(), ThisExePath()); }

// Whether exePath lies inside the folder tempDir - compared without case, on whole folder names, so
// "C:\Temp2\x.exe" is not inside "C:\Temp". That is where Explorer puts a program started straight
// from a zip, and it cleans that folder later: a startup entry pointing there breaks, and the
// program itself is gone after a restart.
bool IsInFolder(const std::wstring& exePath, const std::wstring& tempDir);
// The same for a path, against this user's temporary folder (both in their long form, since either
// may come in 8.3 short names).
bool IsInTemporaryFolder(const std::wstring& path);

} // namespace Autostart

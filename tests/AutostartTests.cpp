// Covers the Start-with-Windows logic. Everything runs against a scratch registry key
// under HKCU\Software\EdgeDeckTests, never the user's real startup entries: the
// Autostart functions take their locations as a value for exactly this reason.
//
// What is checked is each way the old code could fail without anyone noticing: an
// entry that Task Manager had switched off still counted as enabled, enabling did not
// undo that, an entry pointing at a deleted file was never repaired, and a repair
// must not take over an entry that points at a different copy that still exists.
#include "TestHarness.h"

#include "Autostart.h"

#include <windows.h>

#include <string>
#include <vector>

namespace {

constexpr wchar_t kRoot[] = L"Software\\EdgeDeckTests";
constexpr wchar_t kRun[] = L"Software\\EdgeDeckTests\\Run";
constexpr wchar_t kApproved[] = L"Software\\EdgeDeckTests\\StartupApproved";
constexpr wchar_t kName[] = L"EdgeDeck";

Autostart::Locations Scratch() { return Autostart::Locations{kRun, kApproved, kName}; }

void ResetRegistry() { RegDeleteTreeW(HKEY_CURRENT_USER, kRoot); }

// The 12-byte value Task Manager writes; only the first byte matters.
void SetApprovedFlag(BYTE first) {
    BYTE data[12] = {first, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    RegSetKeyValueW(HKEY_CURRENT_USER, kApproved, kName, REG_BINARY, data, sizeof(data));
}

bool ApprovedFlagPresent() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kApproved, kName, RRF_RT_REG_BINARY, nullptr, nullptr,
                        &size) == ERROR_SUCCESS;
}

BYTE ApprovedFlagFirstByte() {
    BYTE data[64]{};
    DWORD size = sizeof(data);
    RegGetValueW(HKEY_CURRENT_USER, kApproved, kName, RRF_RT_REG_BINARY, nullptr, data, &size);
    return data[0];
}

void SetRunCommand(const std::wstring& command) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRun, kName, REG_SZ, command.c_str(),
                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
}

std::wstring RunCommand() {
    wchar_t buffer[1024];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, kRun, kName, RRF_RT_REG_SZ, nullptr, buffer, &size) !=
        ERROR_SUCCESS) {
        return L"<none>";
    }
    return buffer;
}

// Real files, because "does it exist" is the thing being decided.
class TempFiles {
public:
    TempFiles() {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        m_dir = std::wstring(temp) + L"EdgeDeckAutostartTest";
        CreateDirectoryW(m_dir.c_str(), nullptr);
    }
    ~TempFiles() {
        for (const std::wstring& file : m_files) DeleteFileW(file.c_str());
        RemoveDirectoryW(m_dir.c_str());
    }
    std::wstring Make(const wchar_t* name) {
        const std::wstring path = m_dir + L"\\" + name;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        m_files.push_back(path);
        return path;
    }
    std::wstring Missing(const wchar_t* name) const { return m_dir + L"\\" + name; }

private:
    std::wstring m_dir;
    std::vector<std::wstring> m_files;
};

void TestCommandToPath() {
    TEST("Autostart: the path is pulled out of a Run command however it was written");
    using Autostart::CommandToPath;
    CHECK(CommandToPath(L"\"C:\\Apps\\EdgeDeck.exe\"") == L"C:\\Apps\\EdgeDeck.exe");
    CHECK(CommandToPath(L"\"C:\\Program Files\\Edge Deck\\EdgeDeck.exe\" --minimized") ==
          L"C:\\Program Files\\Edge Deck\\EdgeDeck.exe");
    CHECK(CommandToPath(L"C:\\Apps\\EdgeDeck.exe") == L"C:\\Apps\\EdgeDeck.exe");
    // Unquoted with arguments: cut at the .exe, which is the only thing that
    // separates a path with spaces from its arguments.
    CHECK(CommandToPath(L"C:\\My Apps\\EdgeDeck.exe /start") == L"C:\\My Apps\\EdgeDeck.exe");
    CHECK(CommandToPath(L"C:\\Apps\\EDGEDECK.EXE") == L"C:\\Apps\\EDGEDECK.EXE");
    CHECK(CommandToPath(L"   \"C:\\Apps\\EdgeDeck.exe\"") == L"C:\\Apps\\EdgeDeck.exe");
    CHECK(CommandToPath(L"\"C:\\Apps\\EdgeDeck.exe") == L"C:\\Apps\\EdgeDeck.exe"); // no closing quote
    CHECK(CommandToPath(L"").empty());
    CHECK(CommandToPath(L"   ").empty());
}

void TestIsEnabledHonoursTaskManager() {
    TEST("Autostart: an entry that Task Manager switched off does not count as enabled");
    ResetRegistry();
    const auto where = Scratch();

    CHECK(!Autostart::IsEnabled(where)); // nothing there

    SetRunCommand(L"\"C:\\Apps\\EdgeDeck.exe\"");
    CHECK(Autostart::IsEnabled(where)); // entry, no flag: enabled by default

    SetApprovedFlag(2);
    CHECK(Autostart::IsEnabled(where));
    SetApprovedFlag(6); // what Task Manager writes after re-enabling something it had disabled
    CHECK(Autostart::IsEnabled(where));

    SetApprovedFlag(3); // what it writes when the user switches the entry off
    CHECK(!Autostart::IsEnabled(where));
    SetApprovedFlag(7);
    CHECK(!Autostart::IsEnabled(where));

    // A flag with no entry is not enabled either.
    ResetRegistry();
    SetApprovedFlag(2);
    CHECK(!Autostart::IsEnabled(where));
    ResetRegistry();
}

void TestEnableClearsTheOffFlag() {
    TEST("Autostart: enabling writes a quoted path and undoes a Task Manager 'off'");
    ResetRegistry();
    TempFiles files;
    const std::wstring exe = files.Make(L"EdgeDeck.exe");
    const auto where = Scratch();

    SetApprovedFlag(3);
    CHECK(!Autostart::IsEnabled(where));

    CHECK(Autostart::SetEnabled(true, where, exe));
    CHECK(Autostart::IsEnabled(where));
    CHECK(!ApprovedFlagPresent());
    CHECK(RunCommand() == L"\"" + exe + L"\"");
    ResetRegistry();
}

void TestDisableRemovesBoth() {
    TEST("Autostart: disabling removes the entry and the flag, and is safe to repeat");
    ResetRegistry();
    const auto where = Scratch();

    SetRunCommand(L"\"C:\\Apps\\EdgeDeck.exe\"");
    SetApprovedFlag(3);
    CHECK(Autostart::SetEnabled(false, where));
    CHECK(!Autostart::IsEnabled(where));
    CHECK(RunCommand() == L"<none>");
    CHECK(!ApprovedFlagPresent());

    CHECK(Autostart::SetEnabled(false, where)); // already gone: still a success
    ResetRegistry();
}

void TestRepairLeavesWhatIsFine() {
    TEST("Autostart: repair creates nothing, and leaves a working or foreign entry alone");
    ResetRegistry();
    TempFiles files;
    const auto where = Scratch();
    const std::wstring installed = files.Make(L"installed.exe");
    const std::wstring running = files.Make(L"running.exe");
    const std::wstring other = files.Make(L"other.exe");

    // No entry: the user never asked for autostart, so none is created.
    CHECK(Autostart::RepairPath(where, running, installed) == Autostart::Repair::NotEnabled);
    CHECK(RunCommand() == L"<none>");

    // An entry that works.
    SetRunCommand(L"\"" + installed + L"\"");
    CHECK(Autostart::RepairPath(where, running, installed) == Autostart::Repair::Valid);
    CHECK(RunCommand() == L"\"" + installed + L"\"");

    // An entry for a different copy that still exists - a development build beside
    // the installed one. A copy started for a test must not take it over.
    SetRunCommand(L"\"" + other + L"\"");
    CHECK(Autostart::RepairPath(where, running, installed) == Autostart::Repair::Valid);
    CHECK(RunCommand() == L"\"" + other + L"\"");
    ResetRegistry();
}

void TestRepairRepointsABrokenEntry() {
    TEST("Autostart: an entry pointing at a deleted file is repointed, preferring the installed copy");
    ResetRegistry();
    TempFiles files;
    const auto where = Scratch();
    const std::wstring installed = files.Make(L"installed.exe");
    const std::wstring running = files.Make(L"running.exe");
    const std::wstring gone = files.Missing(L"deleted.exe");

    SetRunCommand(L"\"" + gone + L"\"");
    CHECK(Autostart::RepairPath(where, running, installed) == Autostart::Repair::Repointed);
    CHECK(RunCommand() == L"\"" + installed + L"\"");

    // With no installed copy it falls back to the one that is running.
    SetRunCommand(L"\"" + gone + L"\"");
    CHECK(Autostart::RepairPath(where, running, files.Missing(L"not-installed.exe")) ==
          Autostart::Repair::Repointed);
    CHECK(RunCommand() == L"\"" + running + L"\"");
    ResetRegistry();
}

void TestRepairGivesUpHonestly() {
    TEST("Autostart: with nothing to point at, repair reports failure and changes nothing");
    ResetRegistry();
    TempFiles files;
    const auto where = Scratch();
    const std::wstring gone = files.Missing(L"deleted.exe");

    SetRunCommand(L"\"" + gone + L"\"");
    CHECK(Autostart::RepairPath(where, files.Missing(L"a.exe"), files.Missing(L"b.exe")) ==
          Autostart::Repair::Failed);
    CHECK(RunCommand() == L"\"" + gone + L"\"");
    ResetRegistry();
}

void TestRepairDoesNotTouchTheUsersChoice() {
    TEST("Autostart: repairing the path leaves Task Manager's on/off flag alone");
    ResetRegistry();
    TempFiles files;
    const auto where = Scratch();
    const std::wstring installed = files.Make(L"installed.exe");

    SetRunCommand(L"\"" + files.Missing(L"deleted.exe") + L"\"");
    SetApprovedFlag(3); // the user switched it off
    CHECK(Autostart::RepairPath(where, files.Missing(L"r.exe"), installed) ==
          Autostart::Repair::Repointed);
    CHECK_EQ(static_cast<int>(ApprovedFlagFirstByte()), 3); // still off: their decision
    CHECK(!Autostart::IsEnabled(where));
    ResetRegistry();
}

void TestPreferredPath() {
    TEST("Autostart: the installed copy is preferred when it exists");
    TempFiles files;
    const std::wstring installed = files.Make(L"installed.exe");
    CHECK(Autostart::PreferredPath(installed, L"C:\\Dev\\build\\EdgeDeck.exe") == installed);
    CHECK(Autostart::PreferredPath(files.Missing(L"nope.exe"), L"C:\\Dev\\build\\EdgeDeck.exe") ==
          L"C:\\Dev\\build\\EdgeDeck.exe");
}

} // namespace

void RunAutostartTests() {
    TestCommandToPath();
    TestIsEnabledHonoursTaskManager();
    TestEnableClearsTheOffFlag();
    TestDisableRemovesBoth();
    TestRepairLeavesWhatIsFine();
    TestRepairRepointsABrokenEntry();
    TestRepairGivesUpHonestly();
    TestRepairDoesNotTouchTheUsersChoice();
    TestPreferredPath();
    ResetRegistry();
}

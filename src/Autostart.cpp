#include "Autostart.h"

#include <windows.h>

#include <cwctype>

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"EdgeDeck";

bool FileExists(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadCommand(const Autostart::Locations& where, std::wstring& command) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, where.runKey, where.valueName, RRF_RT_REG_SZ, nullptr,
                     nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return false;
    }
    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, where.runKey, where.valueName, RRF_RT_REG_SZ, nullptr,
                     buffer.data(), &bytes) != ERROR_SUCCESS) {
        return false;
    }
    while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
    command = std::move(buffer);
    return true;
}

bool WriteCommand(const Autostart::Locations& where, const std::wstring& exePath) {
    // Quoted so a path with spaces (OneDrive folders, "Program Files") still launches.
    const std::wstring command = L"\"" + exePath + L"\"";
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    return RegSetKeyValueW(HKEY_CURRENT_USER, where.runKey, where.valueName, REG_SZ,
                           command.c_str(), bytes) == ERROR_SUCCESS;
}

// Task Manager stores 12 bytes per entry. The first byte is even when the entry is
// enabled (2, or 6 after it was re-enabled) and odd when the user switched it off
// (3). Only that lowest bit is relied on, so the other flags and the timestamp in
// the rest of the value are left to Windows.
bool SwitchedOffInTaskManager(const Autostart::Locations& where) {
    BYTE data[64]{};
    DWORD size = sizeof(data);
    const LONG result = RegGetValueW(HKEY_CURRENT_USER, where.approvedKey, where.valueName,
                                     RRF_RT_REG_BINARY, nullptr, data, &size);
    return result == ERROR_SUCCESS && size >= 1 && (data[0] & 1) != 0;
}

// A value or key that was never there is as good as removed.
bool DeleteValue(const wchar_t* subKey, const wchar_t* name) {
    const LONG result = RegDeleteKeyValueW(HKEY_CURRENT_USER, subKey, name);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND ||
           result == ERROR_PATH_NOT_FOUND;
}
} // namespace

namespace Autostart {

Locations Default() { return Locations{kRunKey, kApprovedKey, kValueName}; }

bool IsEnabled(const Locations& where) {
    std::wstring command;
    return ReadCommand(where, command) && !SwitchedOffInTaskManager(where);
}

bool SetEnabled(bool enabled, const Locations& where, const std::wstring& exePath) {
    if (!enabled) {
        // Both go: leaving the flag behind would make the next enable look switched
        // off. Evaluated separately so a failure on one does not skip the other.
        const bool entryGone = DeleteValue(where.runKey, where.valueName);
        const bool flagGone = DeleteValue(where.approvedKey, where.valueName);
        return entryGone && flagGone && !IsEnabled(where);
    }

    const std::wstring path = exePath.empty() ? PreferredPath() : exePath;
    if (path.empty() || !WriteCommand(where, path)) return false;
    if (!DeleteValue(where.approvedKey, where.valueName)) return false;

    // Read it back: a write that succeeded but does not show up as enabled is a
    // failure the user has to hear about, not something to assume away.
    return IsEnabled(where);
}

Repair RepairPath(const Locations& where, const std::wstring& runningExe,
                  const std::wstring& installedExe) {
    std::wstring command;
    if (!ReadCommand(where, command)) return Repair::NotEnabled;

    if (FileExists(CommandToPath(command))) return Repair::Valid;

    const std::wstring target = PreferredPath(installedExe, runningExe);
    if (!FileExists(target) || !WriteCommand(where, target)) return Repair::Failed;
    return Repair::Repointed;
}

std::wstring CommandToPath(const std::wstring& command) {
    size_t begin = 0;
    while (begin < command.size() && iswspace(command[begin])) ++begin;
    if (begin == command.size()) return L"";

    if (command[begin] == L'"') {
        const size_t close = command.find(L'"', begin + 1);
        return close == std::wstring::npos ? command.substr(begin + 1)
                                           : command.substr(begin + 1, close - begin - 1);
    }

    // Unquoted: Windows itself would try each prefix up to a space. The one thing
    // worth handling is arguments after an .exe, since a path with spaces and no
    // quotes cannot be told apart otherwise.
    std::wstring rest = command.substr(begin);
    for (size_t i = 0; i + 4 <= rest.size(); ++i) {
        if (rest[i] == L'.' && towlower(rest[i + 1]) == L'e' && towlower(rest[i + 2]) == L'x' &&
            towlower(rest[i + 3]) == L'e') {
            return rest.substr(0, i + 4);
        }
    }
    while (!rest.empty() && iswspace(rest.back())) rest.pop_back();
    return rest;
}

std::wstring InstalledPath() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"";
    return std::wstring(buffer, length) + L"\\EdgeDeck\\bin\\EdgeDeck.exe";
}

std::wstring ThisExePath() {
    // MAX_PATH is not a limit on modern Windows, so grow until it fits.
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return L"";
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring PreferredPath(const std::wstring& installedExe, const std::wstring& runningExe) {
    return FileExists(installedExe) ? installedExe : runningExe;
}

} // namespace Autostart

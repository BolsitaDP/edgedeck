#include "Autostart.h"

#include <windows.h>
#include <string>

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"EdgeDeck";
} // namespace

namespace Autostart {

bool IsEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ, nullptr, nullptr,
                         nullptr) == ERROR_SUCCESS;
}

bool SetEnabled(bool enabled) {
    if (!enabled) {
        LONG r = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
    }

    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;

    // Quoted so a path with spaces (OneDrive folders, "Program Files") still launches.
    std::wstring command = L"\"" + std::wstring(path, len) + L"\"";
    DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, command.c_str(),
                            bytes) == ERROR_SUCCESS;
}

} // namespace Autostart

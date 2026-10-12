#include "Actions.h"

#include <windows.h>
#include <shellapi.h>
#include <shldisp.h>

#include <cstdio>

namespace Actions {

namespace {

// Fallback used only if the shell COM object is unavailable: simulate the
// Win+D shortcut, which is what "show desktop" is bound to system-wide.
bool SimulateWinD() {
    INPUT inputs[4] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LWIN;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'D';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'D';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_LWIN;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    return SendInput(4, inputs, sizeof(INPUT)) == 4;
}

} // namespace

bool ShowDesktopToggle() {
    // ToggleDesktop() only exists on IShellDispatch4+ (IShellDispatch itself
    // predates it), so request that interface directly.
    IShellDispatch4* shellDispatch = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellDispatch4,
                                   reinterpret_cast<void**>(&shellDispatch));
    if (SUCCEEDED(hr) && shellDispatch) {
        HRESULT toggleResult = shellDispatch->ToggleDesktop();
        shellDispatch->Release();
        if (SUCCEEDED(toggleResult)) return true;
    }

    return SimulateWinD();
}

std::wstring ExpandVariables(const std::wstring& text) {
    if (text.find(L'%') == std::wstring::npos) return text;
    const DWORD needed = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (needed == 0) return text;
    std::wstring out(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(text.c_str(), out.data(), needed);
    if (written == 0 || written > needed) return text;
    out.resize(written - 1); // drop the terminator it counts
    return out;
}

std::wstring DescribeShellError(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_BAD_PATHNAME:
            return L"not found";
        case ERROR_ACCESS_DENIED:
            return L"access denied";
        case ERROR_CANCELLED:
            return L"cancelled";
        case ERROR_NO_ASSOCIATION:
        case SE_ERR_NOASSOC:
            return L"nothing is set to open that kind of file";
        case ERROR_BAD_FORMAT:
            return L"not a valid program";
        default: {
            wchar_t text[48];
            swprintf_s(text, L"Windows error %lu", error);
            return text;
        }
    }
}

bool Run(const QuickAction& action, std::wstring& problem) {
    if (action.target == QuickActions::kShowDesktop) {
        if (ShowDesktopToggle()) return true;
        problem = L"Windows could not show the desktop.";
        return false;
    }

    const std::wstring target = ExpandVariables(action.target);
    const std::wstring arguments = ExpandVariables(action.arguments);

    // A program is started from its own folder, as double-clicking it would; a web address or a
    // bare name like "notepad.exe" has none to offer.
    std::wstring directory;
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        const size_t slash = target.find_last_of(L"\\/");
        if (slash != std::wstring::npos) directory = target.substr(0, slash);
    }

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    // NO_UI: without it a failure raises the shell's own error box, a dialog that takes focus
    // from whatever the user was doing. The panel reports the failure instead.
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = target.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_SHOWNORMAL;

    SetLastError(0);
    if (ShellExecuteExW(&info)) return true;

    DWORD error = GetLastError();
    // On older paths the reason is in hInstApp, an SE_ERR_* code (32 and below), not in
    // GetLastError.
    const INT_PTR code = reinterpret_cast<INT_PTR>(info.hInstApp);
    if (error == 0 && code > 0 && code <= 32) error = static_cast<DWORD>(code);
    problem = L"Could not open \"" + action.name + L"\": " + DescribeShellError(error) + L".";
    return false;
}

} // namespace Actions

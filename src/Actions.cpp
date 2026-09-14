#include "Actions.h"

#include <windows.h>
#include <shellapi.h>
#include <shldisp.h>

namespace Actions {

bool OpenNotepad() {
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(nullptr, L"open", L"notepad.exe", nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool OpenCalculator() {
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(nullptr, L"open", L"calc.exe", nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

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

} // namespace Actions

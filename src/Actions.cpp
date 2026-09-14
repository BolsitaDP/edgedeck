#include "Actions.h"

#include <windows.h>
#include <shellapi.h>
#include <shldisp.h>

namespace Actions {

void OpenNotepad() {
    ShellExecuteW(nullptr, L"open", L"notepad.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenCalculator() {
    ShellExecuteW(nullptr, L"open", L"calc.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

namespace {

// Fallback used only if the shell COM object is unavailable: simulate the
// Win+D shortcut, which is what "show desktop" is bound to system-wide.
void SimulateWinD() {
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

    SendInput(4, inputs, sizeof(INPUT));
}

} // namespace

void ShowDesktopToggle() {
    IShellDispatch* shellDispatch = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellDispatch,
                                   reinterpret_cast<void**>(&shellDispatch));
    if (SUCCEEDED(hr) && shellDispatch) {
        shellDispatch->ToggleDesktop();
        shellDispatch->Release();
        return;
    }

    SimulateWinD();
}

} // namespace Actions

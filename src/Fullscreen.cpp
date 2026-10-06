#include "Fullscreen.h"

#include <dwmapi.h>

#include <cwchar>
#include <initializer_list>

namespace Fullscreen {

bool CoversMonitor(const RECT& clientArea, const RECT& monitor) {
    if (monitor.right <= monitor.left || monitor.bottom <= monitor.top) return false;
    return clientArea.left <= monitor.left && clientArea.top <= monitor.top &&
           clientArea.right >= monitor.right && clientArea.bottom >= monitor.bottom;
}

bool AppOnMonitor(HMONITOR monitor) {
    if (!monitor) return false;

    HWND foreground = GetForegroundWindow();
    if (!foreground || !IsWindowVisible(foreground) || IsIconic(foreground)) return false;

    // The desktop and the taskbar cover a monitor too, and clicking the desktop
    // makes it the foreground window; none of them is "an app in the way".
    wchar_t className[64]{};
    GetClassNameW(foreground, className, 64);
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        if (wcscmp(className, shell) == 0) return false;
    }

    // A suspended UWP app keeps a window that is still "foreground" but is not shown.
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked) {
        return false;
    }

    if (MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) != monitor) return false;

    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return false;

    RECT client{};
    POINT origin{0, 0};
    if (!GetClientRect(foreground, &client) || !ClientToScreen(foreground, &origin)) return false;
    const RECT clientOnScreen{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    return CoversMonitor(clientOnScreen, info.rcMonitor);
}

} // namespace Fullscreen

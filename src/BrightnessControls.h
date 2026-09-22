#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

// Per-monitor brightness through the Windows monitor-configuration API
// (DDC/CI over dxva2). DDC/CI is slow (tens to hundreds of ms per call), so
// everything here runs on a short-lived thread-pool job started by an
// explicit user action (opening the panel, moving a slider) and reports back
// with PostMessage - no resident thread, timer or polling.
namespace BrightnessControls {

struct MonitorInfo {
    std::wstring name;
    bool supported = false; // false: monitor/driver doesn't expose DDC/CI brightness
    int percent = 0;        // 0-100, only meaningful when supported
};

// Opaque handle to one physical monitor; releases the OS handle when the
// last reference (widget row or in-flight command) goes away.
class MonitorHandle {
public:
    virtual ~MonitorHandle() = default;
};

struct MonitorList {
    std::vector<MonitorInfo> infos;
    std::vector<std::shared_ptr<MonitorHandle>> handles; // same order; null when unsupported
};

// Enumerates every attached monitor and reads its current brightness.
// Posts a `MonitorList*` via notifyMessage/wParam; the receiver owns and
// must delete it.
void RefreshMonitors(HWND notifyWindow, UINT notifyMessage);

// Sets one monitor's brightness (0-100). `tag` is echoed back with the
// outcome so the caller knows which row finished - see UnpackResult.
void SetBrightness(std::shared_ptr<MonitorHandle> monitor, int percent, int tag,
                    HWND notifyWindow, UINT notifyMessage);

void UnpackResult(WPARAM wParam, int& outTag, bool& outSucceeded);

} // namespace BrightnessControls

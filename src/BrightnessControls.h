#pragma once

#include <windows.h>

#include "AsyncResult.h"

#include <memory>
#include <string>
#include <vector>

// Per-monitor brightness through the Windows monitor-configuration API
// (DDC/CI over dxva2). DDC/CI is slow (tens to hundreds of ms per call), so
// everything here runs on a short-lived thread-pool job started by an
// explicit user action (opening the panel, releasing a slider) and reports
// back with PostMessage - no resident thread, timer or polling.
namespace BrightnessControls {

struct MonitorInfo {
    // Stable identity of the physical monitor (device path), so a late reply
    // can be matched against the row it was issued for even if the list has
    // been re-read in the meantime.
    std::wstring devicePath;
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

struct MonitorList : AsyncEnvelope {
    std::uint64_t listGeneration = 0;
    std::vector<MonitorInfo> infos;
    std::vector<std::shared_ptr<MonitorHandle>> handles; // same order; null when unsupported
};

struct SetResult : AsyncEnvelope {
    std::uint64_t listGeneration = 0;
    int row = 0;
    std::wstring devicePath; // the monitor the write was aimed at
    int percent = 0;         // the value that was actually written
    bool succeeded = false;
};

// Enumerates every attached monitor and reads its current brightness.
// Posts a MonitorList* to notifyWindow as notifyMessage, taking ownership
// either way (see PostOrDelete).
void RefreshMonitors(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
                     std::uint64_t listGeneration);

// Sets one monitor's brightness (0-100). Posts a SetResult* carrying the row,
// the monitor identity and the generation of the list the call was made
// against, so the caller can tell whether the outcome is still relevant.
void SetBrightness(std::shared_ptr<MonitorHandle> monitor, int percent, int row,
                   const std::wstring& devicePath, std::uint64_t requestId,
                   std::uint64_t listGeneration, HWND notifyWindow, UINT notifyMessage);

} // namespace BrightnessControls

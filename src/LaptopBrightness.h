#pragma once

#include <windows.h>

#include <string>
#include <vector>

// The brightness of a laptop's own screen. A built-in panel has no DDC/CI - that is a cable
// protocol for external monitors - so Windows controls its backlight through the display driver,
// and offers that to programs through WMI (root\WMI: WmiMonitorBrightness to read it,
// WmiMonitorBrightnessMethods.WmiSetBrightness to change it), which is what its own brightness
// slider uses.
//
// WMI is a COM service with a cost to load, so it is only asked when there is a built-in panel
// that DDC/CI could not reach: a desktop never loads it. Everything here blocks for as long as WMI
// takes and belongs on a worker thread, like the DDC/CI calls it sits beside.
namespace LaptopBrightness {

struct Panel {
    std::wstring instanceName; // WMI's key for it: "DISPLAY\BOE0812\4&2a8b3b5&0&UID265988_0"
    std::wstring edidId;       // "BOE0812": the same id DisplayTopology knows the monitor by
    int percent = 0;           // 0-100
};

// Every panel WMI can set the brightness of, with its brightness now. Empty when there is none (a
// desktop), and when WMI could not be asked at all.
std::vector<Panel> Read();

// Sets that panel's brightness (0-100). False when WMI refused or could not be reached.
bool Set(const std::wstring& instanceName, int percent);

// The EDID id inside a WMI instance name: the second part of "DISPLAY\BOE0812\...", upper case.
// Empty when the name has no such part.
std::wstring EdidIdFromInstanceName(const std::wstring& instanceName);

// Whether a display connection is a screen built into the machine (a laptop's panel, an
// all-in-one's): the internal output and the embedded DisplayPort, UDI and LVDS-era links.
bool IsBuiltIn(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology);

// Which of `panels` is the built-in monitor with EDID id `edidId`: the one with that id, or - when
// ids cannot be matched - the only panel, if there is exactly one panel and one built-in monitor.
// -1 when none can be said to be it.
int MatchPanel(const std::vector<Panel>& panels, const std::wstring& edidId, size_t builtInMonitors);

} // namespace LaptopBrightness

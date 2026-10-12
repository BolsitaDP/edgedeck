#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Which monitors are switched on, and switching them. This is the Windows
// "display configuration" (CCD) API - the same one Settings > Display uses - and
// not the older ChangeDisplaySettingsEx route, because CCD is the only one that
// can enable a monitor that is currently off.
//
// Nothing here knows about windows or painting, so the planning half is covered
// by the unit tests with hand-built topologies.
namespace DisplayTopology {

struct Monitor {
    // EDID vendor + product, e.g. "GSM7768". Unlike the target id or the
    // adapter LUID it survives a reboot and a driver reload, so it is what a
    // profile and the config file refer to. If two identical monitors are
    // attached the second one gets the connector appended so they stay distinct.
    std::wstring id;
    std::wstring name; // friendly name from the EDID, "LG ULTRAWIDE"
    // The GDI device this monitor is currently drawn through ("\\.\DISPLAY2"), which is what ties
    // an EDID id to a window-system monitor handle. Empty while the monitor is switched off.
    std::wstring gdiName;
    LUID adapter{};
    UINT32 targetId = 0;
    bool active = false; // part of the desktop right now
    // Windows' primary monitor (the one at the origin, where the taskbar starts). False for a
    // monitor that is off.
    bool primary = false;
    // Native resolution. Known for a switched-off monitor too, which is the
    // reason it is read from the preferred mode and not from the live one.
    UINT32 nativeWidth = 0;
    UINT32 nativeHeight = 0;
};

// The layouts the Displays tab offers. New ones are added at the end: the order is the order of
// the tab's buttons and of its Ctrl+Alt+Shift+N shortcuts, and people have those in their fingers.
enum class Profile {
    MainMonitors, // everything except the TV
    TvOnly,
    AllMonitors,
    MainOnly,     // the main monitor alone: everything else, the other desk monitors too, is off
};

// Every connected monitor, switched on or not, ordered by id so the order does
// not depend on which ones happen to be active.
std::vector<Monitor> Enumerate();

// The window-system handle of the monitor with this EDID id, or null if it is not plugged in or
// not part of the desktop at the moment. Costs a display-configuration query, so ask when a
// layout is being computed, not on a hot path.
HMONITOR FindHandle(const std::wstring& id);

// --- pure logic -----------------------------------------------------------

// The GDI device name of the active monitor with this id; empty if there is none, or it is off.
std::wstring GdiNameOf(const std::vector<Monitor>& monitors, const std::wstring& id);

// Which monitor is the TV. A non-empty configuredId wins outright (even if that
// monitor is unplugged - then the TV profiles are simply unavailable). With no
// configured id the largest panel is taken, which is the one rule that holds for
// "a 4K TV next to two desktop monitors" without naming hardware.
std::wstring ResolveTvId(const std::vector<Monitor>& monitors, const std::wstring& configuredId);

// Which monitor is "the main one". A non-empty configuredId wins outright (even if that monitor
// is unplugged - then the profile is simply unavailable). Otherwise it is Windows' primary
// monitor, unless that is the TV, which is what the primary becomes while only the TV is on; then
// it is the largest panel that is not the TV. So "main only" means the same monitor whichever
// layout it is chosen from. Empty when there is no candidate.
std::wstring ResolveMainId(const std::vector<Monitor>& monitors, const std::wstring& tvId,
                           const std::wstring& configuredId);

struct ProfileState {
    bool available = false;           // can it be applied with what is plugged in?
    bool active = false;              // is exactly this set switched on right now?
    std::vector<std::wstring> ids;    // the monitors the profile switches on
};

// mainId is only read by Profile::MainOnly (see ResolveMainId); empty makes that profile
// unavailable. MainOnly is also unavailable when it would be the same layout as MainMonitors - no
// other desk monitor to switch off - so two buttons never light up for one layout.
ProfileState Evaluate(Profile profile, const std::vector<Monitor>& monitors,
                      const std::wstring& tvId, const std::wstring& mainId = L"");

// --- applying -------------------------------------------------------------

enum class ApplyResult {
    Success,
    Unchanged,     // already in that layout, nothing was touched
    NotConnected,  // one of the wanted monitors is not plugged in
    Failed,        // Windows rejected it; see ApplyOutcome::error
};

struct ApplyOutcome {
    ApplyResult result = ApplyResult::Failed;
    LONG error = ERROR_SUCCESS;
};

// Switches exactly `wantedIds` on and every other connected monitor off.
// validateOnly runs Windows' own validation and applies nothing, which is how
// the tests exercise this against real hardware without blanking a screen.
// Blocks while the monitors resync (seconds), so call it off the UI thread.
ApplyOutcome Apply(const std::vector<std::wstring>& wantedIds, bool validateOnly = false);

// --- planning, exposed for the tests ---------------------------------------

struct Config {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

struct TargetKey {
    LUID adapter{};
    UINT32 targetId = 0;
    bool operator==(const TargetKey& o) const {
        return adapter.LowPart == o.adapter.LowPart && adapter.HighPart == o.adapter.HighPart &&
               targetId == o.targetId;
    }
};

// Drops every active path whose target is not wanted, keeps the layout of the
// rest (positions and resolutions untouched, modes compacted and re-indexed) and
// slides the survivors so one of them sits at (0,0) - Windows rejects a desktop
// with nothing at the origin. Used when the change only turns monitors off.
Config PlanRemoval(const Config& active, const std::vector<TargetKey>& wanted);

// Picks one path per wanted target out of the full path list, keeping the source
// of a target that is already on and giving a switched-off one the lowest source
// id not taken on its adapter. Mode indices are cleared so Windows chooses the
// resolution and position itself from its saved layout for that exact set of
// monitors. Returns false if some wanted target has no usable path.
bool PlanTopology(const std::vector<DISPLAYCONFIG_PATH_INFO>& allPaths,
                  const std::vector<TargetKey>& wanted,
                  std::vector<DISPLAYCONFIG_PATH_INFO>& out);

} // namespace DisplayTopology

#pragma once

#include <string>
#include <vector>

#include "DisplayTopology.h"

// The layouts the Displays tab switches between: a name and the monitors it leaves on (every other
// connected monitor is switched off). The user's own, kept in config.txt as [layout] sections and
// edited in their own window; with none saved the tab offers "all monitors" and each monitor alone,
// which needs no setup and fits any desk.
//
// The tab used to offer four fixed layouts built around one machine - desk monitors, the TV alone,
// everything, the main monitor alone - and a heuristic for which monitor was the TV. Those four are
// still how an existing setup is carried over (FromProfiles), once, as saved layouts.
namespace DisplayLayouts {

// Ctrl+Alt+Shift+1..9 switch to layouts 1 to 9: more would have no shortcut, and a panel of more
// than nine rows is no longer a glance.
inline constexpr size_t kMaxLayouts = 9;
inline constexpr size_t kMaxNameLength = 40;

struct Layout {
    std::wstring name;
    std::vector<std::wstring> ids; // EDID ids of the monitors it leaves on
};

struct State {
    bool available = false; // every monitor it names is connected (on or off)
    bool active = false;    // exactly its monitors are on right now
    std::vector<std::wstring> missing; // the ones that are not connected
};

// "All monitors" and "Only <name>" for each, when there are at least two monitors (with one there
// is nothing to switch between, and the list is empty).
std::vector<Layout> Automatic(const std::vector<DisplayTopology::Monitor>& monitors);

// The four old fixed layouts, as saved ones, for carrying an existing setup over: those that can be
// worked out from the monitors present, without two that switch on the same set.
std::vector<Layout> FromProfiles(const std::vector<DisplayTopology::Monitor>& monitors,
                                 const std::wstring& tvId, const std::wstring& mainId);

State Evaluate(const Layout& layout, const std::vector<DisplayTopology::Monitor>& monitors);

// A list as it will be kept: names trimmed and capped, ids upper-cased and without repeats, layouts
// with no name or no monitor dropped, at most kMaxLayouts.
std::vector<Layout> Sanitize(std::vector<Layout> layouts);

// config.txt's form of the monitor list: "GSM7768,SAM0F9D".
std::wstring JoinIds(const std::vector<std::wstring>& ids);
std::vector<std::wstring> SplitIds(const std::wstring& text);

// What a layout made from the current state is first called: the names of the monitors that are on,
// "LG ULTRAWIDE + C27R50x", cut to fit a name.
std::wstring NameFor(const std::vector<std::wstring>& ids, const std::vector<DisplayTopology::Monitor>& monitors);

} // namespace DisplayLayouts

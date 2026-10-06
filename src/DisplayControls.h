#pragma once

#include <windows.h>

#include "AsyncResult.h"
#include "DisplayTopology.h"

#include <string>
#include <vector>

// Thread-pool wrappers around DisplayTopology. Reading the monitor list is quick
// but switching them is not: Windows holds SetDisplayConfig for as long as the
// panels take to resync, which is several seconds with a TV in the chain. Both run
// off the UI thread, start only from an explicit user action (opening the panel,
// pressing a button) and report back with one posted message - no resident
// thread, no timer, no polling.
namespace DisplayControls {

struct MonitorList : AsyncEnvelope {
    AsyncKind Kind() const override { return AsyncKind::Displays; }
    std::vector<DisplayTopology::Monitor> monitors;
    std::wstring tvId; // which one of them is the TV, already resolved
};

struct ApplyResult : AsyncEnvelope {
    AsyncKind Kind() const override { return AsyncKind::DisplayApply; }
    DisplayTopology::ApplyOutcome outcome;
    int profileIndex = -1; // the row that asked for it
};

// configuredTvId is the user's override from the config file (may be empty); it
// is passed in by value so the worker never reads shared state.
void Refresh(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
             std::wstring configuredTvId);

void ApplyProfile(std::vector<std::wstring> monitorIds, int profileIndex, HWND notifyWindow,
                  UINT notifyMessage, std::uint64_t requestId);

} // namespace DisplayControls

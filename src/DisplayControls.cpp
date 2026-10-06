#include "DisplayControls.h"

#include "Diagnostics.h"

#include <winrt/Windows.Foundation.h>

namespace DisplayControls {
namespace {

winrt::fire_and_forget RefreshAsync(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
                                    std::wstring configuredTvId) {
    auto* list = new MonitorList();
    list->requestId = requestId;

    co_await winrt::resume_background();

    try {
        list->monitors = DisplayTopology::Enumerate();
        list->tvId = DisplayTopology::ResolveTvId(list->monitors, configuredTvId);
    } catch (...) {
        // An empty list is a valid answer: the panel shows "no monitors found".
    }
    PostOrDelete(notifyWindow, notifyMessage, list);
}

winrt::fire_and_forget ApplyAsync(std::vector<std::wstring> monitorIds, int profileIndex,
                                  HWND notifyWindow, UINT notifyMessage,
                                  std::uint64_t requestId) {
    auto* result = new ApplyResult();
    result->requestId = requestId;
    result->profileIndex = profileIndex;

    co_await winrt::resume_background();

    try {
        result->outcome = DisplayTopology::Apply(monitorIds, false);
    } catch (...) {
        result->outcome.result = DisplayTopology::ApplyResult::Failed;
        result->outcome.error = ERROR_GEN_FAILURE;
    }
    if (result->outcome.result == DisplayTopology::ApplyResult::Failed) {
        Diagnostics::Error("Displays: SetDisplayConfig rejected profile %d (error %ld)",
                           profileIndex, result->outcome.error);
    }
    PostOrDelete(notifyWindow, notifyMessage, result);
}

} // namespace

void Refresh(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
             std::wstring configuredTvId) {
    RefreshAsync(notifyWindow, notifyMessage, requestId, std::move(configuredTvId));
}

void ApplyProfile(std::vector<std::wstring> monitorIds, int profileIndex, HWND notifyWindow,
                  UINT notifyMessage, std::uint64_t requestId) {
    ApplyAsync(std::move(monitorIds), profileIndex, notifyWindow, notifyMessage, requestId);
}

} // namespace DisplayControls

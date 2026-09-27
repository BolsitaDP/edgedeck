#include "BrightnessControls.h"

#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace BrightnessControls {
namespace {

class ConcreteHandle : public MonitorHandle {
public:
    ConcreteHandle(HANDLE h, DWORD minValue, DWORD maxValue)
        : handle(h), minRaw(minValue), maxRaw(maxValue) {}
    ~ConcreteHandle() override {
        if (handle) DestroyPhysicalMonitor(handle);
    }
    ConcreteHandle(const ConcreteHandle&) = delete;
    ConcreteHandle& operator=(const ConcreteHandle&) = delete;

    HANDLE handle;
    DWORD minRaw;
    DWORD maxRaw;
};

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    reinterpret_cast<std::vector<HMONITOR>*>(param)->push_back(monitor);
    return TRUE;
}

// Real model name from the display's EDID ("DELL U2723QE"), matched to the
// HMONITOR through its GDI device name. Empty when Windows only knows it as a
// generic PnP monitor or the lookup fails.
std::wstring FriendlyName(const wchar_t* gdiDeviceName) {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return L"";

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                            modes.data(), nullptr) != ERROR_SUCCESS)
        return L"";

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (wcscmp(source.viewGdiDeviceName, gdiDeviceName) != 0) continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
        return target.monitorFriendlyDeviceName;
    }
    return L"";
}

// One QueryDisplayConfig pass for the whole enumeration, rather than one per
// monitor: the display-config topology is process-wide and doesn't change while
// we iterate, so re-querying it inside the loop was pure duplicated work
// (O(monitors^2) buffer allocations on every panel open).
std::unordered_map<std::wstring, std::wstring> BuildFriendlyNameMap() {
    std::unordered_map<std::wstring, std::wstring> names;

    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return names;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                            modes.data(), nullptr) != ERROR_SUCCESS)
        return names;

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;

        names.emplace(source.viewGdiDeviceName, target.monitorFriendlyDeviceName);
    }
    return names;
}

winrt::fire_and_forget RefreshAsync(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
                                    std::uint64_t listGeneration) {
    auto* list = new MonitorList();
    list->requestId = requestId;
    list->listGeneration = listGeneration;

    co_await winrt::resume_background();

    try {
        std::vector<HMONITOR> monitors;
        EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));

        const auto friendlyNames = BuildFriendlyNameMap();

        // Primary monitor first so row 1 is the screen the tabs live on.
        std::vector<MONITORINFOEXW> details;
        std::vector<HMONITOR> ordered;
        for (int pass = 0; pass < 2; ++pass) {
            for (HMONITOR m : monitors) {
                MONITORINFOEXW mi{};
                mi.cbSize = sizeof(mi);
                if (!GetMonitorInfoW(m, &mi)) continue;
                bool primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                if ((pass == 0) == primary) {
                    ordered.push_back(m);
                    details.push_back(mi);
                }
            }
        }

        size_t number = 0;
        for (size_t idx = 0; idx < ordered.size(); ++idx) {
            DWORD count = 0;
            if (!GetNumberOfPhysicalMonitorsFromHMONITOR(ordered[idx], &count) || count == 0) {
                continue;
            }
            std::vector<PHYSICAL_MONITOR> physical(count);
            if (!GetPhysicalMonitorsFromHMONITOR(ordered[idx], count, physical.data())) continue;

            auto friendlyIt = friendlyNames.find(details[idx].szDevice);
            const wchar_t* modelName =
                (friendlyIt != friendlyNames.end() && !friendlyIt->second.empty())
                    ? friendlyIt->second.c_str()
                    : L"Generic monitor";

            for (DWORD p = 0; p < count; ++p) {
                ++number;
                MonitorInfo info;
                wchar_t label[192];
                swprintf_s(label, L"%zu: %s", number, modelName);
                info.name = label;
                info.devicePath = physical[p].szPhysicalMonitorDescription;

                DWORD minRaw = 0, curRaw = 0, maxRaw = 0;
                HANDLE h = physical[p].hPhysicalMonitor;
                if (GetMonitorBrightness(h, &minRaw, &curRaw, &maxRaw) && maxRaw > minRaw) {
                    info.supported = true;
                    info.percent = static_cast<int>(
                        ((curRaw - minRaw) * 100.0 / (maxRaw - minRaw)) + 0.5);
                    list->handles.push_back(std::make_shared<ConcreteHandle>(h, minRaw, maxRaw));
                } else {
                    DestroyPhysicalMonitor(h);
                    list->handles.push_back(nullptr);
                }
                list->infos.push_back(std::move(info));
            }
        }
    } catch (...) {
        // Partial/empty list is fine; the panel shows what was gathered.
    }

    PostOrDelete(notifyWindow, notifyMessage, list);
}

winrt::fire_and_forget SetAsync(std::shared_ptr<MonitorHandle> monitor, int percent, int row,
                                std::wstring devicePath, std::uint64_t requestId,
                                std::uint64_t listGeneration, HWND notifyWindow,
                                UINT notifyMessage) {
    auto* result = new SetResult();
    result->requestId = requestId;
    result->listGeneration = listGeneration;
    result->row = row;
    result->devicePath = std::move(devicePath);
    result->percent = percent;

    co_await winrt::resume_background();

    bool succeeded = false;
    auto* concrete = static_cast<ConcreteHandle*>(monitor.get());
    if (concrete) {
        percent = std::clamp(percent, 0, 100);
        DWORD range = concrete->maxRaw - concrete->minRaw;
        DWORD raw = concrete->minRaw + static_cast<DWORD>((percent * range + 50) / 100);
        succeeded = SetMonitorBrightness(concrete->handle, raw) != FALSE;
    }
    result->succeeded = succeeded;
    PostOrDelete(notifyWindow, notifyMessage, result);
}

} // namespace

void RefreshMonitors(HWND notifyWindow, UINT notifyMessage, std::uint64_t requestId,
                     std::uint64_t listGeneration) {
    RefreshAsync(notifyWindow, notifyMessage, requestId, listGeneration);
}

void SetBrightness(std::shared_ptr<MonitorHandle> monitor, int percent, int row,
                   const std::wstring& devicePath, std::uint64_t requestId,
                   std::uint64_t listGeneration, HWND notifyWindow, UINT notifyMessage) {
    SetAsync(std::move(monitor), percent, row, devicePath, requestId, listGeneration, notifyWindow,
             notifyMessage);
}

} // namespace BrightnessControls

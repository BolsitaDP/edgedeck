#include "BrightnessControls.h"
#include "LaptopBrightness.h"
#include "Strings.h"

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

    bool Set(int percent) override {
        percent = std::clamp(percent, 0, 100);
        const DWORD range = maxRaw - minRaw;
        const DWORD raw = minRaw + static_cast<DWORD>((percent * range + 50) / 100);
        return SetMonitorBrightness(handle, raw) != FALSE;
    }

    HANDLE handle;
    DWORD minRaw;
    DWORD maxRaw;
};

// A laptop's built-in screen, set through WMI by the key WMI knows it by.
class BuiltInHandle : public MonitorHandle {
public:
    explicit BuiltInHandle(std::wstring instance) : instanceName(std::move(instance)) {}
    bool Set(int percent) override { return LaptopBrightness::Set(instanceName, percent); }
    std::wstring instanceName;
};

// What the display configuration says about one monitor, by its GDI device name.
struct DisplayFacts {
    std::wstring friendlyName; // from the EDID ("DELL U2723QE"); empty for a generic one
    std::wstring edidId;       // "GSM7768", from the monitor's device path
    bool builtIn = false;      // a laptop's own screen (LaptopBrightness::IsBuiltIn)
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
std::unordered_map<std::wstring, DisplayFacts> BuildDisplayFacts() {
    std::unordered_map<std::wstring, DisplayFacts> names;

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

        DisplayFacts facts;
        facts.friendlyName = target.monitorFriendlyDeviceName;
        facts.builtIn = LaptopBrightness::IsBuiltIn(paths[i].targetInfo.outputTechnology);
        // "\\?\DISPLAY#GSM7768#5&187c0955&0&UID4353#{...}": the id is the part after the first '#'.
        const std::wstring path = target.monitorDevicePath;
        const size_t first = path.find(L'#');
        const size_t second = first == std::wstring::npos ? first : path.find(L'#', first + 1);
        if (second != std::wstring::npos) facts.edidId = path.substr(first + 1, second - first - 1);
        names.emplace(source.viewGdiDeviceName, std::move(facts));
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

        const auto displays = BuildDisplayFacts();
        // Built-in screens DDC/CI did not reach: rows waiting for WMI, asked once for all of them.
        struct Waiting {
            size_t row;
            std::wstring edidId;
        };
        std::vector<Waiting> builtInRows;
        size_t builtInCount = 0;

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
            const auto factsIt = displays.find(details[idx].szDevice);
            const DisplayFacts facts = factsIt != displays.end() ? factsIt->second : DisplayFacts{};
            if (facts.builtIn) ++builtInCount;
            const wchar_t* modelName = !facts.friendlyName.empty() ? facts.friendlyName.c_str()
                                       : facts.builtIn            ? TR(BuiltInDisplay)
                                                                  : TR(GenericMonitor);

            DWORD count = 0;
            std::vector<PHYSICAL_MONITOR> physical;
            if (GetNumberOfPhysicalMonitorsFromHMONITOR(ordered[idx], &count) && count > 0) {
                physical.resize(count);
                if (!GetPhysicalMonitorsFromHMONITOR(ordered[idx], count, physical.data())) physical.clear();
            }
            if (physical.empty()) {
                // A built-in screen may have no physical-monitor handle at all; it still gets its
                // row, for WMI to fill in below. Anything else without one is not listed, as before.
                if (!facts.builtIn) continue;
                ++number;
                MonitorInfo info;
                wchar_t label[192];
                swprintf_s(label, L"%zu: %s", number, modelName);
                info.name = label;
                info.devicePath = L"builtin:" + facts.edidId;
                builtInRows.push_back({list->infos.size(), facts.edidId});
                list->infos.push_back(std::move(info));
                list->handles.push_back(nullptr);
                continue;
            }

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
                    if (facts.builtIn) builtInRows.push_back({list->infos.size(), facts.edidId});
                }
                list->infos.push_back(std::move(info));
            }
        }

        // Only now, and only with a built-in screen DDC/CI could not reach: WMI is a COM service
        // with a cost to load, and a desktop never needs it.
        if (!builtInRows.empty()) {
            const std::vector<LaptopBrightness::Panel> panels = LaptopBrightness::Read();
            for (const Waiting& waiting : builtInRows) {
                const int match = LaptopBrightness::MatchPanel(panels, waiting.edidId, builtInCount);
                if (match < 0) continue;
                const LaptopBrightness::Panel& panel = panels[static_cast<size_t>(match)];
                list->infos[waiting.row].supported = true;
                list->infos[waiting.row].percent = panel.percent;
                list->handles[waiting.row] = std::make_shared<BuiltInHandle>(panel.instanceName);
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
    try {
        if (monitor) succeeded = monitor->Set(percent);
    } catch (...) {
        // Nothing here throws on purpose; a write that did is a write that failed.
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

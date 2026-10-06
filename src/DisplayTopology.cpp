#include "DisplayTopology.h"

#include <algorithm>
#include <cwchar>

namespace DisplayTopology {
namespace {

constexpr UINT32 kInvalidIndex = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;

TargetKey KeyOf(const DISPLAYCONFIG_PATH_INFO& path) {
    return TargetKey{path.targetInfo.adapterId, path.targetInfo.id};
}

bool IsActive(const DISPLAYCONFIG_PATH_INFO& path) {
    return (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
}

bool Contains(const std::vector<TargetKey>& keys, const TargetKey& key) {
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool SameLuid(LUID a, LUID b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }

bool QueryConfig(UINT32 flags, Config& out) {
    // The topology can change between asking how big the answer is and asking for
    // it (a monitor waking up, a hot-plug). Windows reports that as
    // ERROR_INSUFFICIENT_BUFFER, and the right response is to ask again.
    for (int attempt = 0; attempt < 4; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount) != ERROR_SUCCESS) {
            return false;
        }
        out.paths.assign(pathCount, DISPLAYCONFIG_PATH_INFO{});
        out.modes.assign(modeCount, DISPLAYCONFIG_MODE_INFO{});
        LONG rc = QueryDisplayConfig(flags, &pathCount, out.paths.data(), &modeCount,
                                     out.modes.data(), nullptr);
        if (rc == ERROR_SUCCESS) {
            out.paths.resize(pathCount);
            out.modes.resize(modeCount);
            return true;
        }
        if (rc != ERROR_INSUFFICIENT_BUFFER) return false;
    }
    return false;
}

// "\\?\DISPLAY#GSM7768#5&187c0955&0&UID4353#{...}" split on '#'.
std::wstring PathSegment(const std::wstring& devicePath, size_t index) {
    size_t start = 0;
    for (size_t i = 0; i < index; ++i) {
        size_t hash = devicePath.find(L'#', start);
        if (hash == std::wstring::npos) return L"";
        start = hash + 1;
    }
    size_t end = devicePath.find(L'#', start);
    return devicePath.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
}

// The GDI name ("\\.\DISPLAY2") of the source an active path draws through - the same name
// GetMonitorInfo reports for that monitor's handle.
std::wstring SourceGdiName(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) return L"";
    return source.viewGdiDeviceName;
}

} // namespace

std::vector<Monitor> Enumerate() {
    std::vector<Monitor> monitors;
    Config all;
    if (!QueryConfig(QDC_ALL_PATHS, all)) return monitors;

    std::vector<std::wstring> instances; // parallel to `monitors`: the connector part of the path
    for (const auto& path : all.paths) {
        if (!path.targetInfo.targetAvailable) continue;

        const TargetKey key = KeyOf(path);
        auto existing = std::find_if(monitors.begin(), monitors.end(), [&](const Monitor& m) {
            return SameLuid(m.adapter, key.adapter) && m.targetId == key.targetId;
        });
        if (existing != monitors.end()) {
            if (IsActive(path)) {
                existing->active = true;
                existing->gdiName = SourceGdiName(path);
            }
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = key.adapter;
        target.header.id = key.targetId;
        const bool named = DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS;

        Monitor m;
        m.adapter = key.adapter;
        m.targetId = key.targetId;
        m.active = IsActive(path);
        if (m.active) m.gdiName = SourceGdiName(path);
        std::wstring devicePath = named ? target.monitorDevicePath : L"";
        m.id = PathSegment(devicePath, 1);
        if (m.id.empty()) m.id = L"TARGET" + std::to_wstring(key.targetId);
        m.name = (named && target.monitorFriendlyDeviceName[0]) ? target.monitorFriendlyDeviceName
                                                                : m.id;

        DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{};
        preferred.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE;
        preferred.header.size = sizeof(preferred);
        preferred.header.adapterId = key.adapter;
        preferred.header.id = key.targetId;
        if (DisplayConfigGetDeviceInfo(&preferred.header) == ERROR_SUCCESS) {
            m.nativeWidth = preferred.width;
            m.nativeHeight = preferred.height;
        }

        instances.push_back(PathSegment(devicePath, 2));
        monitors.push_back(std::move(m));
    }

    // Two identical monitors share an EDID id; tack the connector on so a profile
    // can still tell them apart.
    for (size_t i = 0; i < monitors.size(); ++i) {
        for (size_t j = 0; j < monitors.size(); ++j) {
            if (i != j && monitors[i].id == monitors[j].id) {
                monitors[i].id += L"#" + instances[i];
                break;
            }
        }
    }

    std::sort(monitors.begin(), monitors.end(),
              [](const Monitor& a, const Monitor& b) { return a.id < b.id; });
    return monitors;
}

std::wstring GdiNameOf(const std::vector<Monitor>& monitors, const std::wstring& id) {
    if (id.empty()) return L"";
    for (const Monitor& m : monitors) {
        if (m.id == id && m.active) return m.gdiName;
    }
    return L"";
}

HMONITOR FindHandle(const std::wstring& id) {
    const std::wstring gdiName = GdiNameOf(Enumerate(), id);
    if (gdiName.empty()) return nullptr;

    struct Search {
        const std::wstring* name;
        HMONITOR found;
    } search{&gdiName, nullptr};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) && _wcsicmp(info.szDevice, s->name->c_str()) == 0) {
                s->found = monitor;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

std::wstring ResolveTvId(const std::vector<Monitor>& monitors, const std::wstring& configuredId) {
    if (!configuredId.empty()) return configuredId;
    if (monitors.size() < 2) return L""; // a lone monitor is not "the TV"

    const Monitor* largest = nullptr;
    bool tie = false;
    for (const Monitor& m : monitors) {
        const unsigned long long area =
            static_cast<unsigned long long>(m.nativeWidth) * m.nativeHeight;
        if (!largest) {
            largest = &m;
            continue;
        }
        const unsigned long long best =
            static_cast<unsigned long long>(largest->nativeWidth) * largest->nativeHeight;
        if (area > best) {
            largest = &m;
            tie = false;
        } else if (area == best) {
            tie = true;
        }
    }
    if (!largest || tie || largest->nativeWidth == 0) return L"";
    return largest->id;
}

ProfileState Evaluate(Profile profile, const std::vector<Monitor>& monitors,
                      const std::wstring& tvId) {
    ProfileState state;
    const bool tvPresent =
        !tvId.empty() && std::any_of(monitors.begin(), monitors.end(),
                                     [&](const Monitor& m) { return m.id == tvId; });

    for (const Monitor& m : monitors) {
        const bool isTv = tvPresent && m.id == tvId;
        switch (profile) {
            case Profile::MainMonitors:
                if (!isTv) state.ids.push_back(m.id);
                break;
            case Profile::TvOnly:
                if (isTv) state.ids.push_back(m.id);
                break;
            case Profile::AllMonitors:
                state.ids.push_back(m.id);
                break;
        }
    }

    // "All" with no TV is just "main monitors" under another name, and "TV only"
    // with no TV is nothing: neither is a button worth offering.
    state.available = !state.ids.empty() &&
                      (profile == Profile::MainMonitors || tvPresent);

    bool exactMatch = state.available;
    for (const Monitor& m : monitors) {
        const bool wanted = std::find(state.ids.begin(), state.ids.end(), m.id) != state.ids.end();
        if (m.active != wanted) exactMatch = false;
    }
    state.active = exactMatch;
    return state;
}

Config PlanRemoval(const Config& active, const std::vector<TargetKey>& wanted) {
    Config out;
    std::vector<UINT32> remap(active.modes.size(), kInvalidIndex);
    auto carry = [&](UINT32 oldIndex) -> UINT32 {
        if (oldIndex == kInvalidIndex || oldIndex >= active.modes.size()) return kInvalidIndex;
        if (remap[oldIndex] == kInvalidIndex) {
            remap[oldIndex] = static_cast<UINT32>(out.modes.size());
            out.modes.push_back(active.modes[oldIndex]);
        }
        return remap[oldIndex];
    };

    for (const auto& path : active.paths) {
        if (!IsActive(path) || !Contains(wanted, KeyOf(path))) continue;
        DISPLAYCONFIG_PATH_INFO kept = path;
        kept.sourceInfo.modeInfoIdx = carry(path.sourceInfo.modeInfoIdx);
        kept.targetInfo.modeInfoIdx = carry(path.targetInfo.modeInfoIdx);
        out.paths.push_back(kept);
    }
    if (out.paths.empty()) return out;

    // Find where the origin has to move to. If a surviving monitor is already at
    // (0,0) the layout is untouched; otherwise the first survivor (Windows lists
    // the primary first) becomes the new origin and everything slides with it.
    auto sourceModeOf = [&](const DISPLAYCONFIG_PATH_INFO& path) -> const DISPLAYCONFIG_MODE_INFO* {
        const UINT32 index = path.sourceInfo.modeInfoIdx;
        if (index == kInvalidIndex || index >= out.modes.size()) return nullptr;
        const DISPLAYCONFIG_MODE_INFO& mode = out.modes[index];
        return mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE ? &mode : nullptr;
    };

    bool originPresent = false;
    for (const auto& path : out.paths) {
        const auto* mode = sourceModeOf(path);
        if (mode && mode->sourceMode.position.x == 0 && mode->sourceMode.position.y == 0) {
            originPresent = true;
        }
    }
    if (!originPresent) {
        const auto* first = sourceModeOf(out.paths.front());
        if (first) {
            const LONG dx = first->sourceMode.position.x;
            const LONG dy = first->sourceMode.position.y;
            for (auto& mode : out.modes) {
                if (mode.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) continue;
                mode.sourceMode.position.x -= dx;
                mode.sourceMode.position.y -= dy;
            }
        }
    }
    return out;
}

bool PlanTopology(const std::vector<DISPLAYCONFIG_PATH_INFO>& allPaths,
                  const std::vector<TargetKey>& wanted,
                  std::vector<DISPLAYCONFIG_PATH_INFO>& out) {
    out.clear();

    auto sourceTaken = [&](const DISPLAYCONFIG_PATH_INFO& candidate) {
        return std::any_of(out.begin(), out.end(), [&](const DISPLAYCONFIG_PATH_INFO& chosen) {
            return SameLuid(chosen.sourceInfo.adapterId, candidate.sourceInfo.adapterId) &&
                   chosen.sourceInfo.id == candidate.sourceInfo.id;
        });
    };
    auto chosenAlready = [&](const TargetKey& key) {
        return std::any_of(out.begin(), out.end(),
                           [&](const DISPLAYCONFIG_PATH_INFO& p) { return KeyOf(p) == key; });
    };
    auto take = [&](const DISPLAYCONFIG_PATH_INFO& path) {
        DISPLAYCONFIG_PATH_INFO chosen = path;
        chosen.flags |= DISPLAYCONFIG_PATH_ACTIVE;
        // Windows picks resolution and position from what it saved for this set.
        chosen.sourceInfo.modeInfoIdx = kInvalidIndex;
        chosen.targetInfo.modeInfoIdx = kInvalidIndex;
        out.push_back(chosen);
    };

    // Monitors that are already on keep the source they have, so they go first;
    // a switched-off one then takes whatever source is left.
    for (const TargetKey& key : wanted) {
        for (const auto& path : allPaths) {
            if (!path.targetInfo.targetAvailable || !IsActive(path) || !(KeyOf(path) == key)) continue;
            if (!chosenAlready(key)) take(path);
        }
    }
    for (const TargetKey& key : wanted) {
        if (chosenAlready(key)) continue;
        for (const auto& path : allPaths) {
            if (!path.targetInfo.targetAvailable || !(KeyOf(path) == key)) continue;
            if (sourceTaken(path)) continue;
            take(path);
            break;
        }
    }

    for (const TargetKey& key : wanted) {
        if (!chosenAlready(key)) return false;
    }
    return true;
}

ApplyOutcome Apply(const std::vector<std::wstring>& wantedIds, bool validateOnly) {
    ApplyOutcome outcome;
    if (wantedIds.empty()) {
        outcome.result = ApplyResult::Failed;
        outcome.error = ERROR_INVALID_PARAMETER;
        return outcome;
    }

    const std::vector<Monitor> monitors = Enumerate();
    std::vector<TargetKey> wanted;
    for (const std::wstring& id : wantedIds) {
        auto it = std::find_if(monitors.begin(), monitors.end(),
                               [&](const Monitor& m) { return m.id == id; });
        if (it == monitors.end()) {
            outcome.result = ApplyResult::NotConnected;
            return outcome;
        }
        wanted.push_back(TargetKey{it->adapter, it->targetId});
    }

    Config active;
    if (!QueryConfig(QDC_ONLY_ACTIVE_PATHS, active)) {
        outcome.result = ApplyResult::Failed;
        outcome.error = ERROR_GEN_FAILURE;
        return outcome;
    }

    std::vector<TargetKey> activeKeys;
    for (const auto& path : active.paths) {
        if (IsActive(path) && !Contains(activeKeys, KeyOf(path))) activeKeys.push_back(KeyOf(path));
    }
    const bool allWantedActive = std::all_of(
        wanted.begin(), wanted.end(), [&](const TargetKey& k) { return Contains(activeKeys, k); });
    if (allWantedActive && activeKeys.size() == wanted.size()) {
        outcome.result = ApplyResult::Unchanged;
        return outcome;
    }

    LONG rc;
    if (allWantedActive) {
        // Only turning monitors off: keep every surviving monitor exactly as it is
        // and write that layout back, so what is left does not get rearranged.
        Config plan = PlanRemoval(active, wanted);
        if (plan.paths.empty()) {
            outcome.result = ApplyResult::Failed;
            outcome.error = ERROR_INVALID_PARAMETER;
            return outcome;
        }
        UINT32 flags = SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES;
        flags |= validateOnly ? SDC_VALIDATE : (SDC_APPLY | SDC_SAVE_TO_DATABASE);
        rc = SetDisplayConfig(static_cast<UINT32>(plan.paths.size()), plan.paths.data(),
                              static_cast<UINT32>(plan.modes.size()), plan.modes.data(), flags);
    } else {
        // Turning something on needs a path that is not in the active set, and
        // that path comes with no mode; Windows fills it in from the layout it
        // remembers for this exact combination of monitors.
        Config all;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths;
        if (!QueryConfig(QDC_ALL_PATHS, all) || !PlanTopology(all.paths, wanted, paths)) {
            outcome.result = ApplyResult::NotConnected;
            return outcome;
        }
        UINT32 flags = SDC_TOPOLOGY_SUPPLIED | SDC_ALLOW_PATH_ORDER_CHANGES;
        flags |= validateOnly ? SDC_VALIDATE : SDC_APPLY;
        rc = SetDisplayConfig(static_cast<UINT32>(paths.size()), paths.data(), 0, nullptr, flags);
    }

    outcome.error = rc;
    outcome.result = (rc == ERROR_SUCCESS) ? ApplyResult::Success : ApplyResult::Failed;
    return outcome;
}

} // namespace DisplayTopology

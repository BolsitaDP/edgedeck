// dispcfg - look at and switch which monitors are on, with the same DisplayTopology code the
// Displays tab uses, so a result here is a result for the app.
//
//   dispcfg list [tvId]             connected monitors, which profile matches, the live layout
//   dispcfg validate <main|tv|all>  ask Windows whether that layout is valid; changes nothing
//   dispcfg apply <main|tv|all>     switch to it (the screens blank for a few seconds)
//   dispcfg snap <file>             save the current layout
//   dispcfg restore <file>          put a saved layout back
//
// main = every monitor but the TV, tv = the TV alone, all = everything. tvId overrides which
// monitor is the TV (an EDID id such as SAM7A08); without it the largest panel is taken, as the
// app does.
//
// A snapshot is only good for the session it was taken in: adapter ids (LUIDs) are reassigned
// on reboot and on a driver reload, so restoring an old file fails or does the wrong thing. It is
// a safety net for one test run, not a backup.
#include "DisplayTopology.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace DisplayTopology;

namespace {

std::wstring ToWide(const char* text) {
    const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(length > 0 ? length - 1 : 0), L'\0');
    if (length > 0) MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), length);
    return out;
}

bool ParseProfile(const char* name, Profile& profile) {
    if (std::strcmp(name, "main") == 0) { profile = Profile::MainMonitors; return true; }
    if (std::strcmp(name, "tv") == 0) { profile = Profile::TvOnly; return true; }
    if (std::strcmp(name, "all") == 0) { profile = Profile::AllMonitors; return true; }
    return false;
}

void PrintMonitors(const std::vector<Monitor>& monitors, const std::wstring& tvId) {
    for (const Monitor& m : monitors) {
        wprintf(L"  id=%-10ls name=%-16ls active=%d native=%ux%u target=%u%ls\n", m.id.c_str(),
                m.name.c_str(), m.active ? 1 : 0, m.nativeWidth, m.nativeHeight, m.targetId,
                m.id == tvId ? L"   <== TV" : L"");
    }
}

void PrintActiveLayout() {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(),
                           nullptr) != ERROR_SUCCESS) {
        return;
    }
    for (UINT32 i = 0; i < pathCount; ++i) {
        const UINT32 index = paths[i].sourceInfo.modeInfoIdx;
        if (index >= modeCount) continue;
        const DISPLAYCONFIG_SOURCE_MODE& mode = modes[index].sourceMode;
        std::printf("  layout: target=%u source=%u at (%ld,%ld) %ux%u\n", paths[i].targetInfo.id,
                    paths[i].sourceInfo.id, mode.position.x, mode.position.y, mode.width, mode.height);
    }
}

int Snapshot(const char* file) {
    UINT32 pathCount = 0, modeCount = 0;
    GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(),
                           nullptr) != ERROR_SUCCESS) {
        std::printf("could not read the layout\n");
        return 1;
    }
    FILE* f = nullptr;
    if (fopen_s(&f, file, "wb") != 0 || !f) {
        std::printf("could not write %s\n", file);
        return 1;
    }
    fwrite(&pathCount, sizeof(pathCount), 1, f);
    fwrite(&modeCount, sizeof(modeCount), 1, f);
    fwrite(paths.data(), sizeof(paths[0]), pathCount, f);
    fwrite(modes.data(), sizeof(modes[0]), modeCount, f);
    fclose(f);
    std::printf("snapshot: %u paths, %u modes -> %s\n", pathCount, modeCount, file);
    return 0;
}

int Restore(const char* file) {
    FILE* f = nullptr;
    if (fopen_s(&f, file, "rb") != 0 || !f) {
        std::printf("could not read %s\n", file);
        return 1;
    }
    UINT32 pathCount = 0, modeCount = 0;
    bool ok = fread(&pathCount, sizeof(pathCount), 1, f) == 1 && fread(&modeCount, sizeof(modeCount), 1, f) == 1;
    // A corrupt or foreign file must not turn into a multi-gigabyte allocation.
    ok = ok && pathCount > 0 && pathCount < 64 && modeCount < 256;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(ok ? pathCount : 0);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(ok ? modeCount : 0);
    ok = ok && fread(paths.data(), sizeof(paths[0]), pathCount, f) == pathCount &&
         fread(modes.data(), sizeof(modes[0]), modeCount, f) == modeCount;
    fclose(f);
    if (!ok) {
        std::printf("%s is not a snapshot this tool wrote\n", file);
        return 1;
    }
    const LONG rc = SetDisplayConfig(pathCount, paths.data(), modeCount, modes.data(),
                                     SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES |
                                         SDC_SAVE_TO_DATABASE);
    std::printf("restore: %s (rc=%ld)\n", rc == ERROR_SUCCESS ? "ok" : "FAILED", rc);
    return rc == ERROR_SUCCESS ? 0 : 1;
}

int Usage() {
    std::printf("usage: dispcfg list [tvId] | validate <main|tv|all> | apply <main|tv|all> | "
                "snap <file> | restore <file>\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return Usage();
    const std::string command = argv[1];

    if (command == "snap" && argc >= 3) return Snapshot(argv[2]);
    if (command == "restore" && argc >= 3) return Restore(argv[2]);

    if (command == "list") {
        const std::vector<Monitor> monitors = Enumerate();
        const std::wstring tvId = ResolveTvId(monitors, argc >= 3 ? ToWide(argv[2]) : std::wstring());
        std::printf("%zu monitors\n", monitors.size());
        PrintMonitors(monitors, tvId);
        const char* names[] = {"main", "tv", "all"};
        const Profile profiles[] = {Profile::MainMonitors, Profile::TvOnly, Profile::AllMonitors};
        for (int i = 0; i < 3; ++i) {
            const ProfileState state = Evaluate(profiles[i], monitors, tvId);
            std::printf("  profile %-4s available=%d active=%d ids=", names[i], state.available ? 1 : 0,
                        state.active ? 1 : 0);
            for (const std::wstring& id : state.ids) wprintf(L"%ls ", id.c_str());
            std::printf("\n");
        }
        PrintActiveLayout();
        return 0;
    }

    if ((command == "validate" || command == "apply") && argc >= 3) {
        Profile profile{};
        if (!ParseProfile(argv[2], profile)) return Usage();
        const std::vector<Monitor> monitors = Enumerate();
        const std::wstring tvId = ResolveTvId(monitors, argc >= 4 ? ToWide(argv[3]) : std::wstring());
        const ProfileState state = Evaluate(profile, monitors, tvId);
        if (!state.available) {
            std::printf("profile '%s' is not available with what is connected\n", argv[2]);
            return 2;
        }
        const bool apply = command == "apply";
        const ApplyOutcome outcome = Apply(state.ids, !apply);
        const char* result[] = {"success", "unchanged", "not connected", "failed"};
        std::printf("%s %s -> %s (error %ld)\n", command.c_str(), argv[2],
                    result[static_cast<int>(outcome.result)], outcome.error);
        if (apply) {
            Sleep(2500); // let the monitors settle before reading the layout back
            PrintActiveLayout();
        }
        return (outcome.result == ApplyResult::Success || outcome.result == ApplyResult::Unchanged) ? 0 : 1;
    }

    return Usage();
}

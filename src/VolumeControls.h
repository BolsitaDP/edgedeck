#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

// Output-device volume and mute over the Core Audio API (IAudioEndpointVolume).
//
// Unlike brightness there is no slow, blocking hardware write here: every call
// is an in-process property read or write on an already-open endpoint, so this
// runs synchronously on the UI thread and needs no thread-pool job at all. That
// also means a slider can be dragged with no round trip, unlike the DDC/CI path
// where the write has to be deferred to the button-up.
namespace VolumeControls {

struct DeviceInfo {
    std::wstring id;   // Core Audio endpoint ID, stable for the current device
    std::wstring name; // friendly name, falls back to a trimmed endpoint ID
    int percent = 0;   // 0-100
    bool muted = false;
    bool isDefault = false;
};

class DeviceHandle {
public:
    virtual ~DeviceHandle() = default;
};

// Enumerates active output endpoints. The default device is listed first.
// Returns false only if the audio service could not be reached at all, in which
// case `out` is left empty.
bool RefreshDevices(std::vector<DeviceInfo>& out, std::vector<std::shared_ptr<DeviceHandle>>& handles);

// Reads one device's current volume and mute state into `out`.
bool QueryState(const std::shared_ptr<DeviceHandle>& device, DeviceInfo& out);

// Sets volume (0-100) and mute. Returns false if the endpoint rejected it, in
// which case the caller should re-read the state rather than assume success.
bool SetVolume(const std::shared_ptr<DeviceHandle>& device, int percent, bool muted);

} // namespace VolumeControls

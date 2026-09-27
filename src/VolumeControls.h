#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Output-device volume and mute over the Core Audio API (IAudioEndpointVolume).
//
// Unlike brightness there is no slow, blocking hardware write here: every call
// is an in-process property read or write, so this runs synchronously on the UI
// thread and needs no thread-pool job at all. That also means a slider can be
// dragged with no round trip, unlike the DDC/CI path where the write has to be
// deferred to the button-up.
//
// Rows hold the endpoint *id*, never a COM object.
//
// Holding an open IMMDevice/IAudioEndpointVolume in the UI looked cheaper, and
// it was: reopening on every call is one in-process trip to the audio service,
// which is not measurable next to a repaint. In exchange it removes a lifetime
// that is genuinely hard to get right here. Core Audio's Activate does not make
// the endpoint-volume object keep its IMMDevice alive, the enumerator is released
// as soon as the list is built, and the apartment the objects were created in is
// uninitialised on the way out - so an object kept around and released later, at
// a moment chosen by whoever happened to clear the rows, can be releasing
// something the audio service has already torn down. That is an access
// violation inside a system DLL, minutes after the call that created it, with
// nothing in the app's own code anywhere near the fault. Not holding the objects
// makes that unrepresentable rather than merely unlikely.
namespace VolumeControls {

struct DeviceInfo {
    std::wstring id;   // Core Audio endpoint ID, stable for the current device
    std::wstring name; // friendly name, falls back to a trimmed endpoint ID
    int percent = 0;   // 0-100
    bool muted = false;
    bool isDefault = false;
};

// Enumerates active output endpoints. The default device is listed first.
// Returns false only if the audio service could not be reached at all, in which
// case `out` is left empty.
bool RefreshDevices(std::vector<DeviceInfo>& out);

// Re-reads one device's current volume and mute state into `out`. `out.id` is
// left as the caller set it; the other fields are overwritten.
bool QueryState(const std::wstring& id, DeviceInfo& out);

// Sets volume (0-100) and mute. Returns false if the endpoint rejected it, in
// which case the caller should re-read the state rather than assume success.
bool SetVolume(const std::wstring& id, int percent, bool muted);

} // namespace VolumeControls

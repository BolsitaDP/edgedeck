#include "Diagnostics.h"
#include "VolumeControls.h"

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace VolumeControls {
namespace {

using Microsoft::WRL::ComPtr;

// DEVICE_STATEMASK_ACTIVE. The SDK only defines the _ALL aggregate, so the
// individual bits are spelled out here; enumerating every state instead lists
// several dozen endpoints that are unplugged or not present, none of which can
// hand out a volume control.
#ifndef DEVICE_STATEMASK_ACTIVE
#define DEVICE_STATEMASK_ACTIVE 0x1
#endif

// Core Audio refuses to instantiate anything from a non-initialised apartment.
// The app already runs CoInitializeEx(APARTMENTTHREADED) on the UI thread, but
// a worker thread would not have it, and a future caller might be on one.
class ComApartment {
public:
    ComApartment() { m_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComApartment() {
        if (SUCCEEDED(m_hr)) CoUninitialize();
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    bool Ok() const { return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE; }

private:
    HRESULT m_hr = E_FAIL;
};

// An open endpoint, scoped to a single call. Nothing here escapes the function
// that built it, so there is no window in which an audio object outlives the
// apartment that created it (see the note in VolumeControls.h).
class Endpoint {
public:
    // Finds `id` among the active render endpoints and opens its volume control.
    // This walks the collection rather than asking the enumerator to look the id
    // up directly, because <mmdeviceapi.h> in this SDK declares that vtable slot
    // as GetDevice(pwstrId, ppDevice) - it drops the EDataFlow parameter the real
    // IMMDeviceEnumerator::GetDeviceById takes, so calling it through the header
    // would pass the wrong arguments. EnumAudioEndpoints and Item are declared
    // correctly and cost the same in-process call.
    bool Open(const std::wstring& id) {
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(enumerator.GetAddressOf()))) ||
            !enumerator) {
            return false;
        }

        ComPtr<IMMDeviceCollection> collection;
        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ACTIVE,
                                                  collection.GetAddressOf())) ||
            !collection) {
            return false;
        }

        UINT count = 0;
        if (FAILED(collection->GetCount(&count))) return false;

        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            if (FAILED(collection->Item(i, device.GetAddressOf())) || !device) continue;

            LPWSTR raw = nullptr;
            if (FAILED(device->GetId(&raw)) || !raw) continue;
            const std::wstring candidate = raw;
            CoTaskMemFree(raw);
            if (candidate != id) continue;

            // IMMDevice::Activate takes a flags DWORD before the activation
            // parameters. The interface id and the out-pointer are passed
            // separately rather than through IID_PPV_ARGS, because that macro
            // cannot be applied to a ComPtr member.
            IAudioEndpointVolume* volume = nullptr;
            const HRESULT hr = device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                                reinterpret_cast<void**>(&volume));
            if (FAILED(hr) || !volume) return false;
            volume_.Attach(volume);
            return true;
        }
        return false;
    }

    IAudioEndpointVolume* Volume() const { return volume_.Get(); }

private:
    ComPtr<IAudioEndpointVolume> volume_;
};

// PKEY_Device_FriendlyName, spelled out because <propsys.h> only declares it
// behind a Windows-version guard, and this project targets one SDK. A
// PROPERTYKEY is a GUID plus a property id, and the id is not optional: leaving
// it at 0 makes the lookup silently fail and every device end up showing its
// raw endpoint GUID.
constexpr PROPERTYKEY kDeviceFriendlyName = {
    0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}, 14};

// CLSID_MMDeviceEnumerator.
//
// mmdeviceapi.h declares it as `EXTERN_C const CLSID`, but the import libraries
// shipped with the SDK do not define it: Uuid.lib has no such symbol (verified
// against 10.0.26100.0) and mmdevapi.lib is a stub, so linking the declared name
// gives LNK2019. Because the declaration is `const` rather than `extern`, this
// translation unit may simply define it.
//
// The value must match the registered coclass exactly. Note the last group:
// C457-9291-692E, not the C42C-075C-0A29 that circulates in older snippets -
// with the wrong tail CoCreateInstance fails with CLASS_E_CLASSNOTAVAILABLE
// (0x80040154) and the panel reports no audio devices.
EXTERN_C const CLSID CLSID_MMDeviceEnumerator = {0xbcde0395, 0xe52f, 0x467c,
                                                 {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};

// IID_IAudioEndpointVolume, for the same reason: endpointvolume.h declares it,
// and the SDK's Uuid.lib does not define it.
EXTERN_C const IID IID_IAudioEndpointVolume = {0x5cdf2c82, 0x841e, 0x4546,
                                               {0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a}};

// Short, human-usable stand-in when Windows has no friendly name for an
// endpoint: the Core Audio ID is "{0.0.0.00000000}.{guid}", so the GUID half is
// both unique and recognisable.
std::wstring FallbackName(const std::wstring& id) {
    const size_t dot = id.rfind(L'.');
    std::wstring tail = (dot == std::wstring::npos) ? id : id.substr(dot + 1);
    if (tail.size() > 2 && tail.front() == L'{' && tail.back() == L'}') {
        tail = tail.substr(1, tail.size() - 2);
    }
    return tail.empty() ? L"Speakers" : tail;
}

// Reads level and mute from an endpoint that is already open, straight into the
// fields the caller cares about. Deliberately not routed through QueryState:
// that takes an endpoint *id* and so has to walk the collection to find it, and
// calling it from inside an enumeration pass re-walked the whole collection once
// per device. That one detail is what made listing the endpoints cost 60ms.
bool ReadState(IMMDevice* device, int& percent, bool& muted) {
    IAudioEndpointVolume* volume = nullptr;
    if (FAILED(device->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&volume))) ||
        !volume) {
        return false;
    }

    float level = 0.0f;
    BOOL isMuted = FALSE;
    const bool ok = SUCCEEDED(volume->GetMasterVolumeLevelScalar(&level));
    if (ok) {
        percent = std::clamp(static_cast<int>(std::lround(level * 100.0f)), 0, 100);
    }
    if (SUCCEEDED(volume->GetMute(&isMuted))) {
        muted = isMuted != FALSE;
    }
    volume->Release();
    return ok;
}

std::wstring FriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (!device || FAILED(device->OpenPropertyStore(STGM_READ, store.GetAddressOf())) || !store) {
        return {};
    }

    PROPERTYKEY key = kDeviceFriendlyName;
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

// Friendly names for endpoints already seen.
//
// OpenPropertyStore/GetValue is the expensive part of building the list, and for
// an endpoint an app owns it is a cross-process round trip - around 60ms for a
// typical seven-device machine, against roughly 4ms for the enumeration itself.
// A name does not change while a device stays plugged in, so it is resolved
// once per endpoint id and remembered. Keyed by id, so a device that disappears
// and comes back is still found.
std::wstring CachedFriendlyName(IMMDevice* device, const std::wstring& id) {
    struct Entry {
        std::wstring id;
        std::wstring name;
    };
    static std::vector<Entry> cache;
    static std::mutex cacheMutex;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const auto& entry : cache) {
            if (entry.id == id) return entry.name;
        }
    }

    std::wstring name = FriendlyName(device);

    std::lock_guard<std::mutex> lock(cacheMutex);
    // Bound it: a machine that has hot-plugged many devices over a long session
    // should not accumulate names forever.
    if (cache.size() >= 64) cache.erase(cache.begin());
    cache.push_back(Entry{id, name});
    return name;
}

} // namespace

bool RefreshDevices(std::vector<DeviceInfo>& out) {
    out.clear();

    ComApartment apartment;
    if (!apartment.Ok()) {
        Diagnostics::Error("Volume: COM apartment unavailable");
        return false;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    const HRESULT createHr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                                             CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(enumerator.GetAddressOf()));
    if (FAILED(createHr) || !enumerator) {
        Diagnostics::Error("Volume: no MMDeviceEnumerator: 0x%08lX",
                           static_cast<unsigned long>(createHr));
        return false;
    }

    // Remember the default endpoint so it can be listed first - the device the
    // user actually means is almost always the one they are listening to.
    std::wstring defaultKey;
    {
        ComPtr<IMMDevice> defaultDevice;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                        defaultDevice.GetAddressOf())) &&
            defaultDevice) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(defaultDevice->GetId(&id)) && id) {
                defaultKey = id;
                CoTaskMemFree(id);
            }
        }
    }

    ComPtr<IMMDeviceCollection> collection;
    const HRESULT enumHr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ACTIVE,
                                                          collection.GetAddressOf());
    if (FAILED(enumHr) || !collection) {
        Diagnostics::Error("Volume: EnumAudioEndpoints failed: 0x%08lX",
                           static_cast<unsigned long>(enumHr));
        return false;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        Diagnostics::Error("Volume: GetCount failed");
        return false;
    }
    if (count == 0) {
        // Genuinely worth saying out loud: it is the difference between "you have
        // no sound card" and "the widget is broken".
        Diagnostics::Info("Volume: the system reports no active render endpoints");
        return true;
    }

    struct Pending {
        DeviceInfo info;
        bool isDefault = false;
    };
    std::vector<Pending> pending;

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.GetAddressOf())) || !device) continue;

        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id)) || !id) continue;
        const std::wstring deviceId = id;
        CoTaskMemFree(id);

        Pending p;
        p.info.id = deviceId;
        p.info.name = CachedFriendlyName(device.Get(), deviceId);
        if (p.info.name.empty()) p.info.name = FallbackName(deviceId);
        p.isDefault = (!defaultKey.empty() && deviceId == defaultKey);
        p.info.isDefault = p.isDefault;

        // Read straight off the device already in hand. Everything opened here is
        // released before the loop body ends, and the enumerator and collection go
        // with the function, so no audio object outlives its apartment.
        if (ReadState(device.Get(), p.info.percent, p.info.muted)) {
            pending.push_back(std::move(p));
        }
    }

    // Default device first.
    //
    // The comparator has to be a strict weak ordering. `a.isDefault && !b.isDefault`
    // looks equivalent and is not: it is not transitive, and both std::sort and
    // std::stable_sort are entitled to walk off the ends of the range when handed
    // a comparator like that. It does not fail loudly - it corrupts the heap, and
    // the resulting access violation surfaces somewhere else entirely a few
    // hundred milliseconds later, which is what made this so hard to attribute.
    std::sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) {
        return static_cast<int>(a.isDefault) > static_cast<int>(b.isDefault);
    });

    for (auto& p : pending) out.push_back(std::move(p.info));
    return true;
}

bool RefreshStates(std::vector<DeviceInfo>& devices) {
    if (devices.empty()) return true;

    ComApartment apartment;
    if (!apartment.Ok()) return false;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(enumerator.GetAddressOf()))) ||
        !enumerator) {
        return false;
    }

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ACTIVE,
                                              collection.GetAddressOf())) ||
        !collection) {
        return false;
    }
    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) return false;

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.GetAddressOf())) || !device) continue;

        LPWSTR raw = nullptr;
        if (FAILED(device->GetId(&raw)) || !raw) continue;
        const std::wstring id = raw;
        CoTaskMemFree(raw);

        DeviceInfo* match = nullptr;
        for (auto& candidate : devices) {
            if (candidate.id == id) {
                match = &candidate;
                break;
            }
        }
        // An endpoint in the collection that the panel is not showing is simply
        // not ours to update; one that is missing from the collection keeps its
        // last known state rather than flickering to zero.
        if (!match) continue;

        ReadState(device.Get(), match->percent, match->muted);
    }
    return true;
}

bool QueryState(const std::wstring& id, DeviceInfo& out) {
    if (id.empty()) return false;

    ComApartment apartment;
    if (!apartment.Ok()) return false;

    Endpoint endpoint;
    if (!endpoint.Open(id)) return false;
    IAudioEndpointVolume* volume = endpoint.Volume();
    if (!volume) return false;

    float level = 0.0f;
    BOOL muted = FALSE;
    if (FAILED(volume->GetMasterVolumeLevelScalar(&level))) return false;
    volume->GetMute(&muted);

    out.percent = std::clamp(static_cast<int>(std::lround(level * 100.0f)), 0, 100);
    out.muted = muted != FALSE;
    return true;
}

bool SetVolume(const std::wstring& id, int percent, bool muted) {
    if (id.empty()) return false;
    percent = std::clamp(percent, 0, 100);

    ComApartment apartment;
    if (!apartment.Ok()) return false;

    Endpoint endpoint;
    if (!endpoint.Open(id)) return false;
    IAudioEndpointVolume* volume = endpoint.Volume();
    if (!volume) return false;

    // Mute first: a muted device reports volume 0, so unmuting after setting
    // the level would leave the slider showing a value the user then has to set
    // again.
    if (FAILED(volume->SetMute(muted ? TRUE : FALSE, nullptr))) return false;
    const float level = static_cast<float>(percent) / 100.0f;
    return SUCCEEDED(volume->SetMasterVolumeLevel(level, nullptr));
}

} // namespace VolumeControls

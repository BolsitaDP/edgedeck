#include "LaptopBrightness.h"

#include "Diagnostics.h"

#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace LaptopBrightness {
namespace {

// COM on this thread for as long as the scope lasts: the thread-pool thread this runs on may not
// have it. A thread that already has it in the other mode (RPC_E_CHANGED_MODE) can still talk to
// WMI, and is left as it was.
class ComScope {
public:
    ComScope() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(m_result)) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT m_result;
};

class Bstr {
public:
    explicit Bstr(const wchar_t* text) : m_value(SysAllocString(text)) {}
    ~Bstr() { SysFreeString(m_value); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return m_value; }

private:
    BSTR m_value;
};

ComPtr<IWbemServices> Connect() {
    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)))) {
        return nullptr;
    }
    ComPtr<IWbemServices> services;
    if (FAILED(locator->ConnectServer(Bstr(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                      &services))) {
        return nullptr;
    }
    // The calls are made as this user, which is all reading and setting a backlight needs. If the
    // proxy cannot be told so, the defaults it has are usually the same, and are tried anyway.
    if (FAILED(CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                 RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) {
        Diagnostics::Error("Brightness: could not set WMI's call security; trying with the defaults");
    }
    return services;
}

// A WMI object path quotes the key: backslashes and quotes inside it are escaped.
std::wstring QuoteKey(const std::wstring& value) {
    std::wstring out;
    for (wchar_t c : value) {
        if (c == L'\\' || c == L'"') out += L'\\';
        out += c;
    }
    return out;
}

} // namespace

std::vector<Panel> Read() {
    std::vector<Panel> panels;
    ComScope com;
    ComPtr<IWbemServices> services = Connect();
    if (!services) {
        Diagnostics::Error("Brightness: could not reach WMI for the built-in screen");
        return panels;
    }
    ComPtr<IEnumWbemClassObject> rows;
    // A machine without a panel WMI can dim answers with an error or with nothing; both are "none".
    if (FAILED(services->ExecQuery(Bstr(L"WQL"),
                                   Bstr(L"SELECT InstanceName, CurrentBrightness FROM WmiMonitorBrightness "
                                        L"WHERE Active = TRUE"),
                                   WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &rows))) {
        return panels;
    }
    for (;;) {
        ComPtr<IWbemClassObject> row;
        ULONG returned = 0;
        // Bounded: a driver that never answers costs this read five seconds, not the thread.
        const HRESULT next = rows->Next(5000, 1, &row, &returned);
        // Anything but a row - an error, the time running out, the end - ends the list.
        if (next != S_OK || returned == 0) break;

        VARIANT name;
        VariantInit(&name);
        VARIANT level;
        VariantInit(&level);
        if (SUCCEEDED(row->Get(L"InstanceName", 0, &name, nullptr, nullptr)) && name.vt == VT_BSTR &&
            SUCCEEDED(row->Get(L"CurrentBrightness", 0, &level, nullptr, nullptr))) {
            VARIANT asInt;
            VariantInit(&asInt);
            if (SUCCEEDED(VariantChangeType(&asInt, &level, 0, VT_I4))) {
                Panel panel;
                panel.instanceName = name.bstrVal;
                panel.edidId = EdidIdFromInstanceName(panel.instanceName);
                panel.percent = std::clamp(static_cast<int>(asInt.lVal), 0, 100);
                panels.push_back(std::move(panel));
            }
            VariantClear(&asInt);
        }
        VariantClear(&name);
        VariantClear(&level);
    }
    return panels;
}

bool Set(const std::wstring& instanceName, int percent) {
    ComScope com;
    ComPtr<IWbemServices> services = Connect();
    if (!services) return false;

    ComPtr<IWbemClassObject> methods;
    ComPtr<IWbemClassObject> inDefinition;
    ComPtr<IWbemClassObject> in;
    if (FAILED(services->GetObject(Bstr(L"WmiMonitorBrightnessMethods"), 0, nullptr, &methods, nullptr)) ||
        FAILED(methods->GetMethod(L"WmiSetBrightness", 0, &inDefinition, nullptr)) || !inDefinition ||
        FAILED(inDefinition->SpawnInstance(0, &in))) {
        return false;
    }
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = 0; // seconds the new level is held before Windows may change it again; 0 is "keep it"
    in->Put(L"Timeout", 0, &value, 0);
    value.vt = VT_UI1;
    value.bVal = static_cast<BYTE>(std::clamp(percent, 0, 100));
    in->Put(L"Brightness", 0, &value, 0);

    const std::wstring path = L"WmiMonitorBrightnessMethods.InstanceName=\"" + QuoteKey(instanceName) + L"\"";
    const HRESULT result =
        services->ExecMethod(Bstr(path.c_str()), Bstr(L"WmiSetBrightness"), 0, nullptr, in.Get(), nullptr, nullptr);
    if (FAILED(result)) {
        Diagnostics::Error("Brightness: WMI did not set the built-in screen (0x%08lX)", static_cast<unsigned long>(result));
    }
    return SUCCEEDED(result);
}

std::wstring EdidIdFromInstanceName(const std::wstring& instanceName) {
    const size_t first = instanceName.find(L'\\');
    if (first == std::wstring::npos) return L"";
    const size_t second = instanceName.find(L'\\', first + 1);
    std::wstring id = instanceName.substr(first + 1, second == std::wstring::npos ? std::wstring::npos
                                                                                   : second - first - 1);
    for (wchar_t& c : id) c = static_cast<wchar_t>(std::towupper(c));
    return id;
}

bool IsBuiltIn(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology) {
    switch (technology) {
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS:
            return true;
        default:
            return false;
    }
}

int MatchPanel(const std::vector<Panel>& panels, const std::wstring& edidId, size_t builtInMonitors) {
    if (!edidId.empty()) {
        for (size_t i = 0; i < panels.size(); ++i) {
            if (_wcsicmp(panels[i].edidId.c_str(), edidId.c_str()) == 0) return static_cast<int>(i);
        }
    }
    return (panels.size() == 1 && builtInMonitors == 1) ? 0 : -1;
}

} // namespace LaptopBrightness

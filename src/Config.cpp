#include "Config.h"

#include <windows.h>
#include <fstream>
#include <string>

namespace {

std::wstring ConfigFilePath() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";

    std::wstring dir = std::wstring(buf) + L"\\EdgeDeck";
    CreateDirectoryW(dir.c_str(), nullptr); // ok if it already exists

    return dir + L"\\config.txt";
}

const wchar_t* WidgetTypeToString(WidgetType type) {
    switch (type) {
        case WidgetType::Media: return L"Media";
        case WidgetType::Brightness: return L"Brightness";
        case WidgetType::Lyrics: return L"Lyrics";
        default: return L"QuickActions";
    }
}

WidgetType WidgetTypeFromString(const std::wstring& s) {
    // "Spotify" is accepted for configs saved before the media widget became
    // app-agnostic.
    if (s == L"Media" || s == L"Spotify") return WidgetType::Media;
    if (s == L"Brightness") return WidgetType::Brightness;
    if (s == L"Lyrics") return WidgetType::Lyrics;
    return WidgetType::QuickActions;
}

std::vector<TabSettings> DefaultTabs() {
    return {
        {WidgetType::QuickActions, 0.25f, 26.0f, 76.0f, 300.0f},
        {WidgetType::Media, 0.50f, 26.0f, 76.0f, 300.0f},
        {WidgetType::Brightness, 0.75f, 26.0f, 76.0f, 300.0f},
    };
}

} // namespace

namespace Config {

std::vector<TabSettings> LoadOrDefault() {
    std::vector<TabSettings> result;
    std::wstring path = ConfigFilePath();

    std::wifstream file(path);
    if (file) {
        TabSettings current;
        bool inTab = false;
        std::wstring line;

        auto flush = [&]() {
            if (inTab) result.push_back(current);
        };

        while (std::getline(file, line)) {
            while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
            if (line.empty()) continue;

            if (line == L"[tab]") {
                flush();
                current = TabSettings{};
                inTab = true;
                continue;
            }

            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos || !inTab) continue;
            std::wstring key = line.substr(0, eq);
            std::wstring value = line.substr(eq + 1);

            try {
                if (key == L"widget") current.widgetType = WidgetTypeFromString(value);
                else if (key == L"verticalRatio") current.verticalRatio = std::stof(value);
                else if (key == L"tabWidth") current.tabWidth = std::stof(value);
                else if (key == L"tabHeight") current.tabHeight = std::stof(value);
                else if (key == L"panelWidth") current.panelWidth = std::stof(value);
            } catch (...) {
                // Malformed line - keep whatever default was already set.
            }
        }
        flush();
    }

    return result.empty() ? DefaultTabs() : result;
}

bool Save(const std::vector<TabSettings>& tabs) {
    std::wstring path = ConfigFilePath();
    if (path.empty()) return false;

    std::wofstream file(path, std::ios::trunc);
    if (!file) return false;

    for (const auto& t : tabs) {
        file << L"[tab]\n";
        file << L"widget=" << WidgetTypeToString(t.widgetType) << L"\n";
        file << L"verticalRatio=" << t.verticalRatio << L"\n";
        file << L"tabWidth=" << t.tabWidth << L"\n";
        file << L"tabHeight=" << t.tabHeight << L"\n";
        file << L"panelWidth=" << t.panelWidth << L"\n";
    }
    return true;
}

} // namespace Config

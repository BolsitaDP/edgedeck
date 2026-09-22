#pragma once

#include <windows.h>
#include <vector>
#include "PanelWidget.h"

// The user-configurable subset of a tab's layout. Tab's own TabConfig also
// carries fixed constants (corner radius, animation timing) that aren't
// exposed in the settings UI; App maps between the two.
struct TabSettings {
    WidgetType widgetType = WidgetType::QuickActions;
    float verticalRatio = 0.5f;
    float tabWidth = 26.0f;
    float tabHeight = 76.0f;
    float panelWidth = 300.0f;
};

// Tiny hand-rolled key=value file under %LOCALAPPDATA%\EdgeDeck\ - no JSON
// library, written only when the user hits Save in the settings window.
namespace Config {

std::vector<TabSettings> LoadOrDefault();
bool Save(const std::vector<TabSettings>& tabs);

} // namespace Config
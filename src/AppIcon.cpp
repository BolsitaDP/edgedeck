#include "AppIcon.h"

#include <commctrl.h>

#include "../res/Resource.h"

namespace AppIcon {

HICON Load(HINSTANCE instance, bool wantSmall) {
    // LoadIconMetric picks the image whose size matches what the system draws icons at, for this
    // process's DPI. LoadIcon always gave the 32 px one, which the notification area then scaled
    // down to 16: soft at 100 % and plain wrong on a scaled monitor. It needs comctl32 v6 (the
    // manifest asks for it) and fails without; the plain load below is the way out.
    HICON icon = nullptr;
    if (SUCCEEDED(LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_EDGEDECK),
                                 wantSmall ? LIM_SMALL : LIM_LARGE, &icon)) &&
        icon) {
        return icon;
    }

    const int cx = GetSystemMetrics(wantSmall ? SM_CXSMICON : SM_CXICON);
    const int cy = GetSystemMetrics(wantSmall ? SM_CYSMICON : SM_CYICON);
    icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_EDGEDECK), IMAGE_ICON, cx, cy,
                                         LR_DEFAULTCOLOR));
    if (icon) return icon;

    return static_cast<HICON>(
        LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR | LR_SHARED));
}

} // namespace AppIcon

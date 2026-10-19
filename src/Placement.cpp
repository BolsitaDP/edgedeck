#include "Placement.h"

#include <algorithm>

namespace Placement {

int PanelY(const RECT& tab, int panelHeight, const RECT& monitor) {
    const int centerY = (tab.top + tab.bottom) / 2;
    const int wanted = centerY - panelHeight / 2;
    const int lowest = std::max<int>(monitor.top, monitor.bottom - panelHeight);
    return std::clamp<int>(wanted, monitor.top, lowest);
}

Result Compute(const Input& in) {
    Result out;
    const int monitorHeight = in.monitor.bottom - in.monitor.top;

    // Truncated, as it always was: rounding would move every existing tab by a pixel.
    const int tabY =
        in.monitor.top + static_cast<int>((monitorHeight - in.tabHeight) * in.verticalRatio);
    if (in.edge == ScreenEdge::Left) {
        const int tabX = in.monitor.left;
        out.tab = {tabX, tabY, tabX + in.tabWidth, tabY + in.tabHeight};
        out.panelOpenX = out.tab.right + in.gap;              // beside the tab, towards the screen
        out.panelClosedX = in.monitor.left - in.panelWidth;   // wholly beyond the left edge
    } else {
        const int tabX = in.monitor.right - in.tabWidth;
        out.tab = {tabX, tabY, tabX + in.tabWidth, tabY + in.tabHeight};
        out.panelOpenX = out.tab.left - in.gap - in.panelWidth;
        out.panelClosedX = in.monitor.right;
    }
    out.panelY = PanelY(out.tab, in.panelHeight, in.monitor);
    return out;
}

int ClampTabTop(int top, int tabHeight, const RECT& monitor) {
    const int lowest = std::max<int>(monitor.top, monitor.bottom - tabHeight);
    return std::clamp<int>(top, monitor.top, lowest);
}

float RatioForTabTop(int top, int tabHeight, const RECT& monitor) {
    const int range = (monitor.bottom - monitor.top) - tabHeight;
    if (range <= 0) return 0.0f;
    const int offset = ClampTabTop(top, tabHeight, monitor) - monitor.top;
    if (offset <= 0) return 0.0f;
    if (offset >= range) return 1.0f;
    // Half a pixel past the top: Compute truncates, and a float a hair under the exact quotient
    // would land one pixel higher.
    return (static_cast<float>(offset) + 0.5f) / static_cast<float>(range);
}

RECT VisiblePart(int x, int y, int w, int h, const RECT& monitor) {
    RECT window{x, y, x + w, y + h};
    RECT shown{};
    if (!IntersectRect(&shown, &window, &monitor)) return RECT{0, 0, 0, 0};
    OffsetRect(&shown, -x, -y);
    return shown;
}

} // namespace Placement

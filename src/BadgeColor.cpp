#include "BadgeColor.h"

#include "Renderer.h"

D2D1_COLOR_F HashedBadgeColor(const std::wstring& key) {
    unsigned long long hash = 1469598103934665603ULL; // FNV-1a
    for (wchar_t c : key) {
        hash ^= static_cast<unsigned long long>(c & 0x7F);
        hash *= 1099511628211ULL;
    }

    // HSL -> RGB by hand: D2D1 has no HSL helper, and going through HSL keeps
    // every generated colour at the same lightness and saturation.
    const float hue = static_cast<float>(hash % 360) / 360.0f;
    const float saturation = PanelTheme::Current().dark ? 0.50f : 0.58f;
    const float lightness = PanelTheme::Current().dark ? 0.54f : 0.44f;

    auto channel = [&](float p, float q, float t) {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f / 2.0f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };

    const float q = lightness * (1.0f + saturation) - lightness * saturation;
    const float p = 2.0f * lightness - q;
    return D2D1::ColorF(channel(p, q, hue + 1.0f / 3.0f), channel(p, q, hue),
                        channel(p, q, hue - 1.0f / 3.0f), 1.0f);
}

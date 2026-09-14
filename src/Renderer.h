#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

// One entry in the panel's action list.
struct PanelItem {
    std::wstring text;
};

// Shared logical (96-DPI) layout constants for the panel's content.
// Kept here so EdgeWindow's hit-testing and Renderer's drawing never drift apart.
namespace PanelLayout {
constexpr float PaddingX = 18.0f;
constexpr float TitleHeight = 44.0f;
constexpr float RowHeight = 44.0f;
constexpr float BottomPadding = 10.0f;
} // namespace PanelLayout

// Thin wrapper around a single Direct2D HWND render target + DirectWrite text
// formats. One instance is owned per top-level window (tab, panel). All
// drawing happens on demand from WM_PAINT - there is no render loop.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool AttachToWindow(HWND hwnd);
    void OnResize(UINT widthPx, UINT heightPx);
    void SetRenderDpi(float dpi);

    // Draws the small edge tab. w/h/radius are logical (96-DPI) units.
    void DrawTab(bool hovered, float w, float h, float radius, const wchar_t* glyph);

    // Draws the flyout panel with its title and action rows.
    void DrawPanel(float w, float h, float radius,
                   const std::vector<PanelItem>& items, int hoveredIndex);

private:
    bool EnsureTarget();
    bool EnsureBrush(Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush, D2D1_COLOR_F color);
    void DiscardTarget();

    HWND m_hwnd = nullptr;
    float m_renderDpi = 96.0f;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_primaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_secondaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_dividerBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_hoverBrush;
};

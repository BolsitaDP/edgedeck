#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "PanelWidget.h"

// Shared logical (96-DPI) layout constants for the panel's chrome (title +
// pin button). Widgets own their own internal layout constants; this
// namespace is only the part Tab's hit-testing and Renderer's chrome
// drawing must agree on.
namespace PanelLayout {
constexpr float PaddingX = 18.0f;
constexpr float ChromeHeight = 44.0f;
constexpr float BottomPadding = 10.0f;
constexpr float PinButtonSize = 24.0f;

inline D2D1_RECT_F PinButtonRect(float panelWidth) {
    float top = (ChromeHeight - PinButtonSize) / 2.0f;
    float right = panelWidth - PaddingX;
    return D2D1::RectF(right - PinButtonSize, top, right, top + PinButtonSize);
}
} // namespace PanelLayout

// Thin wrapper around a single Direct2D HWND render target + DirectWrite text
// formats. One instance is owned per top-level window (tab, panel per Tab
// instance). All drawing happens on demand from WM_PAINT - there is no
// render loop. Implements IPanelPainter so widgets can draw without owning
// any D2D resources themselves.
class Renderer : public IPanelPainter {
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

    // Draws the panel chrome (title + pin glyph + divider), then delegates
    // the content area to the widget via IPanelPainter.
    void DrawPanel(float w, float h, PanelWidget* widget, bool pinned, bool pinHovered);

    // IPanelPainter
    void DrawRow(D2D1_RECT_F rect, const wchar_t* text, bool hovered) override;
    void DrawLabel(D2D1_RECT_F rect, const wchar_t* text, bool muted) override;
    void DrawBadge(D2D1_RECT_F rect, const wchar_t* letters, D2D1_COLOR_F color) override;
    void DrawIconButton(D2D1_RECT_F rect, const wchar_t* glyph, bool hovered, bool enabled) override;
    void DrawPauseButton(D2D1_RECT_F rect, bool hovered, bool enabled) override;
    void DrawSlider(D2D1_RECT_F track, float value01, bool active, bool enabled) override;
    void DrawValueText(D2D1_RECT_F rect, const wchar_t* text, bool muted) override;

private:
    bool EnsureTarget();
    void DrawPin(D2D1_RECT_F rect, bool pinned, bool hovered);
    bool EnsureBrush(Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& brush, D2D1_COLOR_F color);
    void DiscardTarget();

    HWND m_hwnd = nullptr;
    float m_renderDpi = 96.0f;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_target;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_primaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_secondaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_dividerBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_hoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_controlBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_accentBrush;
};

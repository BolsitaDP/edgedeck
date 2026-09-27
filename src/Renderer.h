#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

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

// Palette resolved from the user's theme. Light/dark is a system setting that
// can change while the app runs, so it is re-read on WM_SETTINGCHANGE rather
// than hardcoded - and high contrast takes over the colours entirely, because a
// hand-picked background is exactly what high contrast exists to replace.
struct PanelTheme {
    bool highContrast = false;
    bool dark = true;

    D2D1_COLOR_F panelBg{};
    D2D1_COLOR_F tabBg{};
    D2D1_COLOR_F tabHoverBg{};
    D2D1_COLOR_F rowHoverBg{};
    D2D1_COLOR_F textPrimary{};
    D2D1_COLOR_F textSecondary{};
    D2D1_COLOR_F divider{};
    D2D1_COLOR_F controlBg{};
    D2D1_COLOR_F accent{};

    static PanelTheme Current();
};

// Thin Direct2D + DirectWrite wrapper. One instance is owned per top-level
// window (a tab, or a panel per Tab instance). All drawing happens on demand
// from WM_PAINT - there is no render loop. Implements IPanelPainter so widgets
// can draw without owning any D2D resources themselves.
//
// Compositing note: this uses ID2D1HwndRenderTarget on a WS_EX_LAYERED window
// with SetLayeredWindowAttributes, not a DIB plus UpdateLayeredWindow. Both
// routes were built and measured. The per-pixel-alpha route is the one that
// would give antialiased rounded corners - a SetWindowRgn region is a 1-bit
// mask and its edges are visibly jagged at 125% scaling and above - but every
// D2D surface available without a DXGI device (ID2D1HwndRenderTarget and
// ID2D1DCRenderTarget both included) presents through GDI, which discards the
// alpha channel. The result is a window that composites successfully and shows
// nothing at all. Getting real per-pixel alpha means moving to
// ID2D1DeviceContext + ID2D1Device1 over a DXGI device and blitting the bitmap
// by hand, which is a much larger change than the corner quality is worth here.
class Renderer : public IPanelPainter {
public:
    Renderer() = default;
    ~Renderer() override;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool AttachToWindow(HWND hwnd, bool rounded);
    void OnResize(UINT widthPx, UINT heightPx);
    void SetRenderDpi(float dpi);

    // No-op kept for symmetry: an ID2D1HwndRenderTarget reports
    // D2DERR_RECREATE_TARGET, which the paint path below handles by dropping and
    // rebuilding the target.
    void Present();

    // Renders the whole surface. Returns false if the target could not be built.
    bool Composite();

    // Draws the small edge tab. w/h are logical (96-DPI) units.
    void DrawTab(bool hovered, float w, float h, const wchar_t* glyph);

    // Draws the panel chrome (title + pin glyph + divider), then delegates
    // the content area to the widget via IPanelPainter.
    void DrawPanel(float w, float h, PanelWidget* widget, bool pinned, bool pinHovered);

    // IPanelPainter
    void DrawRow(D2D1_RECT_F rect, const wchar_t* text, bool hovered) override;
    void DrawLabel(D2D1_RECT_F rect, const wchar_t* text, bool muted) override;
    void DrawLabelPair(D2D1_RECT_F rect, const wchar_t* primary,
                       const wchar_t* secondary) override;
    void DrawBadge(D2D1_RECT_F rect, const wchar_t* letters, D2D1_COLOR_F color) override;
    void DrawIconButton(D2D1_RECT_F rect, const wchar_t* glyph, bool hovered, bool enabled) override;
    void DrawPrevButton(D2D1_RECT_F rect, bool hovered, bool enabled) override;
    void DrawPlayButton(D2D1_RECT_F rect, bool hovered, bool enabled) override;
    void DrawPauseButton(D2D1_RECT_F rect, bool hovered, bool enabled) override;
    void DrawNextButton(D2D1_RECT_F rect, bool hovered, bool enabled) override;
    void DrawSlider(D2D1_RECT_F track, float value01, float minValue01, float maxValue01,
                    bool active, bool enabled) override;
    void DrawValueText(D2D1_RECT_F rect, const wchar_t* text, bool muted) override;
    void DrawProgress(D2D1_RECT_F rect, float value01, bool indeterminate) override;
    void DrawMuteButton(D2D1_RECT_F rect, bool muted, bool hovered) override;

private:
    bool EnsureTarget();
    void DiscardTarget();
    void ResetBrushes();
    bool EnsureBrushes();
    void FillRoundedPanel(float w, float h, float radius, D2D1_COLOR_F color);
    void DrawPin(D2D1_RECT_F rect, bool pinned, bool hovered);
    ID2D1SolidColorBrush* DrawTransportButton(D2D1_RECT_F rect, bool hovered, bool enabled);
    void DrawSpeaker(D2D1_RECT_F rect, ID2D1SolidColorBrush* brush);

    HWND m_hwnd = nullptr;
    bool m_rounded = true;
    float m_renderDpi = 96.0f;
    PanelTheme m_theme = PanelTheme::Current();

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_target;
    UINT m_widthPx = 0;
    UINT m_heightPx = 0;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_primaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_secondaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_dividerBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_hoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_controlBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_accentBrush;
};

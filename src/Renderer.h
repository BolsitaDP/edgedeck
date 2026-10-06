#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <memory>
#include <string>

#include "LayeredTarget.h"
#include "PanelWidget.h"

// How see-through a tab or panel is, out of 255. The old value of 235 washed out
// the text along with the background; 246 keeps a hint of translucency while
// staying legible over any wallpaper. Applied to the whole window, whichever way
// it is composited.
constexpr BYTE kSurfaceAlpha = 246;

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
//
// Current() returns a cached copy. The previous version queried the registry and
// SystemParametersInfo on every single paint, which is a synchronising disk-backed
// call roughly fifteen times per animation frame per tab. Refresh() does the real
// work and is driven by the theme-related window messages instead, so the cost is
// paid once per actual change and never in the paint path.
struct PanelTheme {
    bool highContrast = false;
    bool dark = true;

    D2D1_COLOR_F panelBg{};   // the panel surface
    D2D1_COLOR_F tabBg{};     // the rail, unhovered
    D2D1_COLOR_F tabHoverBg{};// hovered: matches panelBg so the panel reads as
                              // growing out of the tab
    D2D1_COLOR_F rowHoverBg{};
    D2D1_COLOR_F textPrimary{};
    D2D1_COLOR_F textSecondary{};
    D2D1_COLOR_F divider{};
    D2D1_COLOR_F controlBg{}; // slider track, button fill
    D2D1_COLOR_F accent{};    // the user's Windows accent, legible on panelBg

    // A one-pixel inner outline. On the per-pixel-alpha path it is the rim of a
    // shape that is already antialiased; on the region fallback SetWindowRgn is a
    // 1-bit mask, so the window edge is aliased against whatever is behind it, and
    // a deliberate border reads as a designed edge and hides the staircase.
    D2D1_COLOR_F edge{};

    // The cached palette. Cheap by construction - a struct copy, no syscalls.
    static const PanelTheme& Current();

    // Re-reads the system state. Returns true if anything actually changed, so
    // callers can skip a relayout when the user merely moved a window around.
    static bool Refresh();

    // Same colours, different role, so a renderer can react to a palette change
    // without comparing floats.
    bool operator==(const PanelTheme& other) const;
    bool operator!=(const PanelTheme& other) const { return !(*this == other); }
};

// Thin Direct2D + DirectWrite wrapper. One instance is owned per top-level
// window (a tab, or a panel per Tab instance). All drawing happens on demand
// from WM_PAINT - there is no render loop. Implements IPanelPainter so widgets
// can draw without owning any D2D resources themselves.
//
// Compositing: two ways to put a surface on a WS_EX_LAYERED window, and a window
// uses exactly one of them for its whole life (IsLayered).
//
//  - Per-pixel alpha (preferred): Direct2D draws into a premultiplied DIB through
//    a software ID2D1DCRenderTarget, and UpdateLayeredWindow presents it. The
//    shape of the window is its alpha, so the rounded corners are antialiased at
//    any scale. See LayeredTarget for why this is software and not D3D.
//  - Region (fallback): an ID2D1HwndRenderTarget plus SetLayeredWindowAttributes
//    and a SetWindowRgn rounded region. The region is a 1-bit mask, so its edge is
//    staircased from 125% scaling up; kept for the case where the DC render target
//    cannot be created.
//
// An ID2D1HwndRenderTarget composites through GDI, which drops the alpha channel,
// so it cannot be the surface for the first route - which is what an earlier
// attempt ran into. Everything the widgets and the paint code touch is an
// ID2D1RenderTarget, so none of them know which route is in use.
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

    // True when this window is on the per-pixel-alpha path. Fixed by
    // AttachToWindow. Such a window must not carry a window region (a region clips
    // the alpha away again) and must not call SetLayeredWindowAttributes (mutually
    // exclusive with UpdateLayeredWindow).
    bool IsLayered() const { return m_layeredMode; }

    // Corner radius in logical (96-DPI) units, for the per-pixel-alpha path where
    // the shape is drawn rather than cut. Ignored on the region path, whose radius
    // is applied by Tab.
    void SetCornerRadius(float logicalRadius) { m_cornerRadius = logicalRadius; }

    // Draws the small edge tab. w/h are logical (96-DPI) units.
    void DrawTab(bool hovered, float w, float h, const wchar_t* glyph);

    // Draws the panel chrome (title + pin glyph + divider), then delegates
    // the content area to the widget via IPanelPainter. A non-empty `notice`
    // takes the title's place, in a warning colour, until the caller stops
    // passing it: the header is the one spot that is always on screen with the
    // panel, so a problem report there needs no extra room and cannot hide the
    // content it is about.
    void DrawPanel(float w, float h, PanelWidget* widget, bool pinned, bool pinHovered,
                   const wchar_t* notice = nullptr);

    // 1px inner outline. Called after Clear in both DrawTab and DrawPanel.
    void DrawEdge(float w, float h);

    // IPanelPainter
    void DrawRow(D2D1_RECT_F rect, const wchar_t* text, bool hovered) override;
    void DrawLabel(D2D1_RECT_F rect, const wchar_t* text, bool muted) override;
    void DrawLabelPair(D2D1_RECT_F rect, const wchar_t* primary,
                       const wchar_t* secondary) override;
    void DrawBadge(D2D1_RECT_F rect, const wchar_t* letters, D2D1_COLOR_F color) override;
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

    // Picks up a new palette and drops the brushes that baked in the old one.
    void AdoptTheme();
    // Paints the surface's background. On the region path that is a plain Clear;
    // on the per-pixel-alpha path the surface is cleared to transparent and the
    // rounded shape is filled, so the corners keep an alpha of zero.
    void PaintBackground(float w, float h, D2D1_COLOR_F color);
    void FillRoundedPanel(float w, float h, float radius, D2D1_COLOR_F color);
    void PresentLayered();
    void DrawPin(D2D1_RECT_F rect, bool pinned, bool hovered);
    ID2D1SolidColorBrush* DrawTransportButton(D2D1_RECT_F rect, bool hovered, bool enabled);
    void DrawSpeaker(D2D1_RECT_F rect, ID2D1SolidColorBrush* brush);

    HWND m_hwnd = nullptr;
    bool m_rounded = true;
    float m_renderDpi = 96.0f;
    PanelTheme m_theme = PanelTheme::Current();

    // m_target is whichever surface this window uses - the layered DC render
    // target or the HWND render target - and ID2D1RenderTarget is all the drawing
    // code ever touches. m_layered owns the first kind; it is null on the region
    // path.
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> m_target;
    std::unique_ptr<LayeredTarget> m_layered;
    bool m_layeredMode = false;
    float m_cornerRadius = 10.0f;
    UINT m_widthPx = 0;
    UINT m_heightPx = 0;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_primaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_secondaryTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_dividerBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_hoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_controlBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_accentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_edgeBrush;
};

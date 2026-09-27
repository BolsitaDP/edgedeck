#include "Diagnostics.h"
#include "Renderer.h"

#include <d2d1helper.h>
#include <uxtheme.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {

// ---------------------------------------------------------------------------
// Process-wide Direct2D / DirectWrite resources
// ---------------------------------------------------------------------------

ID2D1Factory* D2DFactory() {
    static ComPtr<ID2D1Factory> factory = [] {
        ComPtr<ID2D1Factory> f;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, f.GetAddressOf());
        return f;
    }();
    return factory.Get();
}

IDWriteFactory* DWriteFactoryPtr() {
    static ComPtr<IDWriteFactory> factory = [] {
        ComPtr<IDWriteFactory> f;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(f.GetAddressOf()));
        return f;
    }();
    return factory.Get();
}

// Text formats are shared by every window. They carry no DPI: the render
// target's SetDpi scales them, so one set of formats serves every monitor.
//
// Every one of them trims. A panel is 300 logical pixels wide, and a track name,
// a monitor model or an app name will happily be longer than that. Without
// trimming DirectWrite clips the string wherever the layout box runs out, which
// lands mid-glyph and reads as a rendering bug. Word granularity drops the
// overflow a whole word at a time, so a long label degrades into a shorter true
// one instead of nonsense.
//
// A trailing ellipsis would need a custom IDWriteInlineObject, which is a lot
// of COM for a cosmetic detail, and DWRITE_TRIMMING's own `delimiter` field is
// a *leading* marker (meant for path-style truncation), so it cannot do the job.
IDWriteTextFormat* MakeFormat(float sizePx, DWRITE_FONT_WEIGHT weight, bool centered) {
    if (!DWriteFactoryPtr()) return nullptr;
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(DWriteFactoryPtr()->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            sizePx, L"en-us", f.GetAddressOf()))) {
        return nullptr;
    }
    f->SetParagraphAlignment(centered ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                     : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (centered) f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_WORD, 0, L'\0'};
    f->SetTrimming(&trimming, nullptr);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // Deliberately leaked: one set of formats per size for the life of the
    // process, shared by every window and every render target.
    return f.Detach();
}

IDWriteTextFormat* TitleFormat() {
    static IDWriteTextFormat* fmt = MakeFormat(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    return fmt;
}
IDWriteTextFormat* ItemFormat() {
    static IDWriteTextFormat* fmt = MakeFormat(13.5f, DWRITE_FONT_WEIGHT_REGULAR, false);
    return fmt;
}
IDWriteTextFormat* DetailFormat() {
    static IDWriteTextFormat* fmt = MakeFormat(11.5f, DWRITE_FONT_WEIGHT_REGULAR, false);
    return fmt;
}
IDWriteTextFormat* GlyphFormat() {
    static IDWriteTextFormat* fmt = MakeFormat(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    return fmt;
}
IDWriteTextFormat* ControlFormat() {
    static IDWriteTextFormat* fmt = MakeFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    return fmt;
}

// Push-pin silhouette (cap, stem, flared plate, needle) pointing down, in a box
// centred on the origin. Device-independent, so it is built once per process and
// shared by every panel's render target.
ID2D1PathGeometry* PinGeometry() {
    static ComPtr<ID2D1PathGeometry> geometry = []() -> ComPtr<ID2D1PathGeometry> {
        ID2D1Factory* factory = D2DFactory();
        ComPtr<ID2D1PathGeometry> g;
        if (!factory || FAILED(factory->CreatePathGeometry(g.GetAddressOf()))) return nullptr;

        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(g->Open(sink.GetAddressOf()))) return nullptr;

        static const D2D1_POINT_2F pts[] = {
            {-3.5f, -8.5f}, {3.5f, -8.5f}, {3.5f, -6.5f}, {2.0f, -6.5f}, {2.0f, -1.5f},
            {5.0f, 0.5f},   {5.0f, 2.0f},  {0.7f, 2.0f},  {0.0f, 8.5f},  {-0.7f, 2.0f},
            {-5.0f, 2.0f},  {-5.0f, 0.5f}, {-2.0f, -1.5f}, {-2.0f, -6.5f}, {-3.5f, -6.5f},
        };
        sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(pts + 1, static_cast<UINT32>(sizeof(pts) / sizeof(pts[0]) - 1));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return nullptr;
        return g;
    }();
    return geometry.Get();
}

// Speaker body for the mute toggle: a small filled box plus a cone, drawn as one
// path. Same deal as the pin - built once, shared.
ID2D1PathGeometry* SpeakerGeometry() {
    static ComPtr<ID2D1PathGeometry> geometry = []() -> ComPtr<ID2D1PathGeometry> {
        ID2D1Factory* factory = D2DFactory();
        ComPtr<ID2D1PathGeometry> g;
        if (!factory || FAILED(factory->CreatePathGeometry(g.GetAddressOf()))) return nullptr;

        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(g->Open(sink.GetAddressOf()))) return nullptr;

        // Coil box on the left, cone opening to the right.
        sink->BeginFigure(D2D1_POINT_2F{-6.5f, -2.0f}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1_POINT_2F{-3.5f, -2.0f});
        sink->AddLine(D2D1_POINT_2F{-0.5f, -5.0f});
        sink->AddLine(D2D1_POINT_2F{-0.5f, 5.0f});
        sink->AddLine(D2D1_POINT_2F{-3.5f, 2.0f});
        sink->AddLine(D2D1_POINT_2F{-6.5f, 2.0f});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return nullptr;
        return g;
    }();
    return geometry.Get();
}

// Unit right-pointing triangle: (0,0), (1,0.5), (0,1). Callers scale and place
// it with a transform, which is also how the previous-track shape is produced
// (by mirroring the X scale).
ID2D1PathGeometry* TriangleGeometry() {
    static ComPtr<ID2D1PathGeometry> geometry = []() -> ComPtr<ID2D1PathGeometry> {
        ID2D1Factory* factory = D2DFactory();
        ComPtr<ID2D1PathGeometry> g;
        if (!factory || FAILED(factory->CreatePathGeometry(g.GetAddressOf()))) return nullptr;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(g->Open(sink.GetAddressOf()))) return nullptr;
        sink->BeginFigure(D2D1_POINT_2F{0.0f, 0.0f}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1_POINT_2F{1.0f, 0.5f});
        sink->AddLine(D2D1_POINT_2F{0.0f, 1.0f});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return nullptr;
        return g;
    }();
    return geometry.Get();
}

ID2D1StrokeStyle* RoundStrokeStyle() {
    static ComPtr<ID2D1StrokeStyle> style = []() -> ComPtr<ID2D1StrokeStyle> {
        ID2D1Factory* factory = D2DFactory();
        ComPtr<ID2D1StrokeStyle> s;
        if (!factory) return nullptr;
        D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND);
        if (FAILED(factory->CreateStrokeStyle(props, nullptr, 0, s.GetAddressOf()))) return nullptr;
        return s;
    }();
    return style.Get();
}

// Saves, composes and restores the render target's transform around a primitive
// that needs to rotate or scale itself.
//
// Two things make this necessary rather than merely tidy, and both produce
// layout bugs that look like arithmetic errors in the widget:
//
//   * A primitive that finishes by calling SetTransform(Identity) - the obvious
//     thing to write - silently cancels the chrome offset that DrawPanel
//     installed, so everything the widget draws after that primitive lands at the
//     wrong height.
//
//   * SetTransform REPLACES the matrix, it does not compose onto it. A widget
//     draws in content-local coordinates and the chrome offset lives in the
//     transform, so a local "scale and rotate this icon" has to be multiplied on
//     the right of the current matrix. Setting it outright throws the chrome
//     offset away and the icon flies off to the panel's own origin.
class ScopedTransform {
public:
    explicit ScopedTransform(ID2D1RenderTarget* target) : m_target(target) {
        if (m_target) m_target->GetTransform(&m_previous);
    }
    ~ScopedTransform() {
        if (m_target) m_target->SetTransform(m_previous);
    }
    ScopedTransform(const ScopedTransform&) = delete;
    ScopedTransform& operator=(const ScopedTransform&) = delete;

    // Applies `local` on top of whatever transform is currently in force.
    //
    // Direct2D uses row vectors (p' = p * M), so `A * B` applies A and then B.
    // The chrome offset is already in m_previous and lives in panel coordinates,
    // while `local` is expressed in the widget's content coordinates, so `local`
    // has to be applied first: local * previous. The other order scales
    // coordinates that have already been shifted by the chrome height, which
    // throws the icon clear off the panel.
    void Compose(const D2D1_MATRIX_3X2_F& local) const {
        if (m_target) m_target->SetTransform(local * m_previous);
    }

    // Replaces the transform outright. Only correct where the caller knows the
    // target is at identity - the chrome, before any widget is involved.
    void Replace(const D2D1_MATRIX_3X2_F& absolute) const {
        if (m_target) m_target->SetTransform(absolute);
    }

private:
    ID2D1RenderTarget* m_target = nullptr;
    D2D1_MATRIX_3X2_F m_previous{};
};

} // namespace

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

PanelTheme PanelTheme::Current() {
    PanelTheme t;

    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    t.highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != 0;

    // Dark when the desktop or apps are in dark mode. The registry is the only
    // synchronous answer available to a classic Win32 process; re-reading it on
    // WM_SETTINGCHANGE is enough for a palette refresh.
    DWORD appsUseLightTheme = 1;
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &appsUseLightTheme,
                     nullptr) != ERROR_SUCCESS) {
        appsUseLightTheme = 1;
    }
    t.dark = (appsUseLightTheme == 0);

    if (t.highContrast) {
        // Whatever colours the user chose, honour them: use the system
        // window/button/text colours rather than inventing a palette of our own,
        // which is exactly what high contrast exists to prevent.
        t.panelBg = D2D1::ColorF(GetSysColor(COLOR_WINDOW), 1.0f);
        t.tabBg = D2D1::ColorF(GetSysColor(COLOR_BTNFACE), 1.0f);
        t.tabHoverBg = D2D1::ColorF(GetSysColor(COLOR_BTNHIGHLIGHT), 1.0f);
        t.rowHoverBg = D2D1::ColorF(GetSysColor(COLOR_HIGHLIGHT), 1.0f);
        t.textPrimary = D2D1::ColorF(GetSysColor(COLOR_WINDOWTEXT), 1.0f);
        t.textSecondary = D2D1::ColorF(GetSysColor(COLOR_GRAYTEXT), 1.0f);
        t.divider = D2D1::ColorF(GetSysColor(COLOR_WINDOWTEXT), 0.35f);
        t.controlBg = D2D1::ColorF(GetSysColor(COLOR_BTNFACE), 1.0f);
        t.accent = D2D1::ColorF(GetSysColor(COLOR_HIGHLIGHT), 1.0f);
        return t;
    }

    if (t.dark) {
        t.panelBg = D2D1::ColorF(0.098f, 0.098f, 0.098f, 1.0f);
        t.tabBg = D2D1::ColorF(0.145f, 0.145f, 0.145f, 1.0f);
        t.tabHoverBg = D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f);
        t.rowHoverBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f);
        t.textPrimary = D2D1::ColorF(0.93f, 0.93f, 0.93f, 1.0f);
        t.textSecondary = D2D1::ColorF(0.68f, 0.68f, 0.68f, 1.0f);
        t.divider = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f);
        t.controlBg = D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f);
        t.accent = D2D1::ColorF(0.30f, 0.62f, 0.98f, 1.0f);
    } else {
        t.panelBg = D2D1::ColorF(0.97f, 0.97f, 0.97f, 1.0f);
        t.tabBg = D2D1::ColorF(0.91f, 0.91f, 0.91f, 1.0f);
        t.tabHoverBg = D2D1::ColorF(0.84f, 0.84f, 0.84f, 1.0f);
        t.rowHoverBg = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f);
        t.textPrimary = D2D1::ColorF(0.11f, 0.11f, 0.11f, 1.0f);
        t.textSecondary = D2D1::ColorF(0.42f, 0.42f, 0.42f, 1.0f);
        t.divider = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.12f);
        t.controlBg = D2D1::ColorF(0.90f, 0.90f, 0.90f, 1.0f);
        t.accent = D2D1::ColorF(0.16f, 0.47f, 0.85f, 1.0f);
    }
    return t;
}

// ---------------------------------------------------------------------------
// Surface lifecycle
// ---------------------------------------------------------------------------

Renderer::~Renderer() { DiscardTarget(); }

void Renderer::DiscardTarget() {
    ResetBrushes();
    m_target.Reset();
    m_widthPx = 0;
    m_heightPx = 0;
}

void Renderer::ResetBrushes() {
    m_primaryTextBrush.Reset();
    m_secondaryTextBrush.Reset();
    m_dividerBrush.Reset();
    m_hoverBrush.Reset();
    m_controlBrush.Reset();
    m_accentBrush.Reset();
}

bool Renderer::AttachToWindow(HWND hwnd, bool rounded) {
    m_hwnd = hwnd;
    m_rounded = rounded;
    m_theme = PanelTheme::Current();
    return TitleFormat() && ItemFormat() && DetailFormat() && GlyphFormat() && ControlFormat();
}

bool Renderer::EnsureTarget() {
    if (!m_hwnd) {
        Diagnostics::Error("Renderer: EnsureTarget with no window");
        return false;
    }

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const UINT width = static_cast<UINT>(std::max<LONG>(0, rc.right - rc.left));
    const UINT height = static_cast<UINT>(std::max<LONG>(0, rc.bottom - rc.top));
    if (width == 0 || height == 0) return false;

    // An ID2D1HwndRenderTarget keeps the pixel size it was created with, so one
    // left over from a previous window size keeps drawing into the old
    // dimensions and gets clipped (or stretched) by the window. Rebuilding
    // whenever the client rect no longer matches is what keeps the two in step.
    if (m_target && m_widthPx == width && m_heightPx == height) return true;
    if (m_target) {
        Diagnostics::Info("Renderer: target was %ux%u, client is now %ux%u - rebuilding",
                          m_widthPx, m_heightPx, width, height);
        DiscardTarget();
    }

    ID2D1Factory* factory = D2DFactory();
    if (!factory) {
        Diagnostics::Error("Renderer: no Direct2D factory");
        return false;
    }

    const HRESULT hr = factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(width, height)),
        m_target.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        Diagnostics::Error("Renderer: CreateHwndRenderTarget(%ux%u) failed: 0x%08lX", width, height,
                           static_cast<unsigned long>(hr));
        m_target.Reset();
        return false;
    }

    m_target->SetDpi(m_renderDpi, m_renderDpi);
    m_widthPx = width;
    m_heightPx = height;
    return true;
}

void Renderer::OnResize(UINT widthPx, UINT heightPx) {
    if (m_widthPx == widthPx && m_heightPx == heightPx) return;
    m_target.Reset();
    m_widthPx = 0;
    m_heightPx = 0;
    ResetBrushes();
}

void Renderer::SetRenderDpi(float dpi) {
    m_renderDpi = dpi;
    if (m_target) m_target->SetDpi(dpi, dpi);
}

void Renderer::Present() {
    // An ID2D1HwndRenderTarget can report D2DERR_RECREATE_TARGET when the device
    // is lost. Every paint path below handles that by dropping the target, so the
    // next EnsureTarget builds a fresh one.
}

bool Renderer::EnsureBrushes() {
    if (!m_target) return false;
    auto make = [&](ComPtr<ID2D1SolidColorBrush>& brush, D2D1_COLOR_F color) {
        if (!brush && FAILED(m_target->CreateSolidColorBrush(color, brush.GetAddressOf()))) {
            return false;
        }
        return true;
    };
    return make(m_primaryTextBrush, m_theme.textPrimary) &&
           make(m_secondaryTextBrush, m_theme.textSecondary) &&
           make(m_dividerBrush, m_theme.divider) && make(m_hoverBrush, m_theme.rowHoverBg) &&
           make(m_controlBrush, m_theme.controlBg) && make(m_accentBrush, m_theme.accent);
}

bool Renderer::Composite() {
    // The target presents itself; reaching into the HDC here would only defeat
    // D2D's own batching. What matters is that it exists and is sized.
    return m_target != nullptr && m_widthPx > 0 && m_heightPx > 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void Renderer::FillRoundedPanel(float w, float h, float /*radius*/, D2D1_COLOR_F color) {
    // The window region already clips the surface to the rounded shape, so this
    // only has to paint the background. Filling a rounded rect anyway keeps the
    // code correct if the region ever goes away, and costs one call.
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(m_target->CreateSolidColorBrush(color, brush.GetAddressOf()))) return;
    m_target->FillRectangle(D2D1::RectF(0.0f, 0.0f, w, h), brush.Get());
}

// No radius parameter: the rounded shape comes from the window region (see the
// note on the class), so the surface itself is a plain rectangle.
void Renderer::DrawTab(bool hovered, float w, float h, const wchar_t* glyph) {
    if (!EnsureTarget()) return;

    // Re-read on every paint: the user can switch between light and dark, or turn
    // high contrast on, while the app is running, and a tab is cheap to repaint.
    m_theme = PanelTheme::Current();

    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    m_target->Clear(hovered ? m_theme.tabHoverBg : m_theme.tabBg);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(m_target->CreateSolidColorBrush(m_theme.textSecondary, brush.GetAddressOf()))) {
        m_target->DrawText(glyph, static_cast<UINT32>(wcslen(glyph)), GlyphFormat(),
                           D2D1::RectF(0.0f, 0.0f, w, h), brush.Get());
    }

    if (m_target->EndDraw() == D2DERR_RECREATE_TARGET) DiscardTarget();
}

void Renderer::DrawPanel(float w, float h, PanelWidget* widget, bool pinned, bool pinHovered) {
    if (!EnsureTarget()) return;

    // The window is the authority on its own size. Tab's idea of the panel
    // geometry can be one frame behind a resize - the panel grows when a widget
    // reports more rows, and it is repainted in the same pass - so the logical
    // width and height handed in are corrected against the real client rect
    // before anything is laid out. Drawing a 300-wide layout into a 304-wide
    // window is harmless; drawing a 128-tall layout into a window the target
    // thinks is 60 tall is what produced overlapping rows.
    const float scale = m_renderDpi / 96.0f;
    const float clientWidth = static_cast<float>(m_widthPx) / scale;
    const float clientHeight = static_cast<float>(m_heightPx) / scale;
    if (clientWidth > 0.0f) w = clientWidth;
    if (clientHeight > 0.0f) h = clientHeight;

    m_theme = PanelTheme::Current();
    if (!EnsureBrushes()) return;

    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    m_target->Clear(m_theme.panelBg);

    const D2D1_RECT_F pinRect = PanelLayout::PinButtonRect(w);
    const D2D1_RECT_F titleRect = D2D1::RectF(PanelLayout::PaddingX, 0.0f, pinRect.left - 6.0f,
                                              PanelLayout::ChromeHeight);
    const wchar_t* title = widget ? widget->PanelTitle() : L"EdgeDeck";
    m_target->DrawText(title, static_cast<UINT32>(wcslen(title)), TitleFormat(), titleRect,
                       m_primaryTextBrush.Get());

    DrawPin(pinRect, pinned, pinHovered);

    m_target->DrawLine(D2D1::Point2F(PanelLayout::PaddingX, PanelLayout::ChromeHeight),
                       D2D1::Point2F(w - PanelLayout::PaddingX, PanelLayout::ChromeHeight),
                       m_dividerBrush.Get(), 1.0f);

    if (widget) {
        // The widget draws in content-local coordinates; the chrome offset lives
        // in the transform, and the scope puts it back afterwards.
        const ScopedTransform keep(m_target.Get());
        m_target->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, PanelLayout::ChromeHeight));
        widget->Draw(*this, w, h - PanelLayout::ChromeHeight - PanelLayout::BottomPadding);
    }

    if (m_target->EndDraw() == D2DERR_RECREATE_TARGET) DiscardTarget();
}

void Renderer::DrawPin(D2D1_RECT_F rect, bool pinned, bool hovered) {
    if (hovered) {
        m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, 6.0f, 6.0f), m_hoverBrush.Get());
    }

    ID2D1PathGeometry* geometry = PinGeometry();
    if (!geometry) return;
    ID2D1StrokeStyle* stroke = RoundStrokeStyle();

    // Pinned: upright, solid accent. Unpinned: tilted, outline only - the
    // classic "pin it" / "pinned" pair, readable at a glance.
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const ScopedTransform keep(m_target.Get());
    keep.Compose(D2D1::Matrix3x2F::Rotation(pinned ? 0.0f : 40.0f) *
                  D2D1::Matrix3x2F::Translation(cx, cy));

    if (pinned) {
        m_target->FillGeometry(geometry, m_accentBrush.Get());
        m_target->DrawGeometry(geometry, m_accentBrush.Get(), 1.0f, stroke);
    } else {
        ID2D1SolidColorBrush* brush =
            hovered ? m_primaryTextBrush.Get() : m_secondaryTextBrush.Get();
        m_target->DrawGeometry(geometry, brush, 1.4f, stroke);
    }
}

void Renderer::DrawRow(D2D1_RECT_F rect, const wchar_t* text, bool hovered) {
    if (!m_target) return;
    if (hovered) {
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(rect.left - 10.0f, rect.top + 2.0f, rect.right + 10.0f, rect.bottom - 2.0f),
            6.0f, 6.0f);
        m_target->FillRoundedRectangle(rr, m_hoverBrush.Get());
    }
    m_target->DrawText(text, static_cast<UINT32>(wcslen(text)), ItemFormat(), rect,
                       m_primaryTextBrush.Get());
}

void Renderer::DrawLabel(D2D1_RECT_F rect, const wchar_t* text, bool muted) {
    if (!m_target) return;
    ID2D1SolidColorBrush* brush = muted ? m_secondaryTextBrush.Get() : m_primaryTextBrush.Get();
    m_target->DrawText(text, static_cast<UINT32>(wcslen(text)), ItemFormat(), rect, brush);
}

void Renderer::DrawLabelPair(D2D1_RECT_F rect, const wchar_t* primary, const wchar_t* secondary) {
    if (!m_target) return;
    const float half = rect.bottom - rect.top;
    m_target->DrawText(primary, static_cast<UINT32>(wcslen(primary)), ItemFormat(),
                       D2D1::RectF(rect.left, rect.top, rect.right, rect.top + half * 0.55f),
                       m_primaryTextBrush.Get());
    m_target->DrawText(secondary, static_cast<UINT32>(wcslen(secondary)), DetailFormat(),
                       D2D1::RectF(rect.left, rect.top + half * 0.5f, rect.right, rect.bottom),
                       m_secondaryTextBrush.Get());
}

void Renderer::DrawBadge(D2D1_RECT_F rect, const wchar_t* letters, D2D1_COLOR_F color) {
    if (!m_target) return;

    // The colour varies per app, so this brush cannot come from the fixed
    // palette cache. Creating a solid-colour brush is cheap, and this only ever
    // happens during an actual WM_PAINT (event-driven, not per frame).
    ComPtr<ID2D1SolidColorBrush> badgeBrush;
    if (FAILED(m_target->CreateSolidColorBrush(color, badgeBrush.GetAddressOf()))) return;

    m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, 6.0f, 6.0f), badgeBrush.Get());
    m_target->DrawText(letters, static_cast<UINT32>(wcslen(letters)), GlyphFormat(), rect,
                       m_primaryTextBrush.Get());
}

void Renderer::DrawIconButton(D2D1_RECT_F rect, const wchar_t* glyph, bool hovered, bool enabled) {
    if (!m_target) return;
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 5.0f, 5.0f);
    m_target->FillRoundedRectangle(rr, m_controlBrush.Get());
    if (hovered && enabled) m_target->FillRoundedRectangle(rr, m_hoverBrush.Get());
    m_target->DrawText(glyph, static_cast<UINT32>(wcslen(glyph)), GlyphFormat(), rect,
                       enabled ? m_primaryTextBrush.Get() : m_secondaryTextBrush.Get());
}

ID2D1SolidColorBrush* Renderer::DrawTransportButton(D2D1_RECT_F rect, bool hovered, bool enabled) {
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 5.0f, 5.0f);
    m_target->FillRoundedRectangle(rr, m_controlBrush.Get());
    if (hovered && enabled) m_target->FillRoundedRectangle(rr, m_hoverBrush.Get());
    return enabled ? m_primaryTextBrush.Get() : m_secondaryTextBrush.Get();
}

void Renderer::DrawPrevButton(D2D1_RECT_F rect, bool hovered, bool enabled) {
    if (!m_target) return;
    ID2D1SolidColorBrush* brush = DrawTransportButton(rect, hovered, enabled);

    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float h = (rect.bottom - rect.top) * 0.34f; // half-height
    const float w = (rect.right - rect.left) * 0.20f;
    const float barWidth = w * 0.55f;

    // Mirrored triangle with its apex pointing left, plus a vertical bar on the
    // right. Drawn as vectors because Segoe UI has no glyph for U+23EE: as a
    // font glyph it rendered as a "tofu" box.
    {
        const ScopedTransform keep(m_target.Get());
        keep.Compose(D2D1::Matrix3x2F::Scale(-w, 2.0f * h) *
                     D2D1::Matrix3x2F::Translation(cx + w * 0.5f, cy - h));
        ID2D1PathGeometry* triangle = TriangleGeometry();
        if (triangle) m_target->FillGeometry(triangle, brush);
    }
    m_target->FillRectangle(
        D2D1::RectF(cx + w * 0.4f, cy - h, cx + w * 0.4f + barWidth, cy + h), brush);
}

void Renderer::DrawNextButton(D2D1_RECT_F rect, bool hovered, bool enabled) {
    if (!m_target) return;
    ID2D1SolidColorBrush* brush = DrawTransportButton(rect, hovered, enabled);

    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float h = (rect.bottom - rect.top) * 0.34f;
    const float w = (rect.right - rect.left) * 0.20f;
    const float barWidth = w * 0.55f;

    {
        const ScopedTransform keep(m_target.Get());
        keep.Compose(D2D1::Matrix3x2F::Scale(w, 2.0f * h) *
                     D2D1::Matrix3x2F::Translation(cx - w * 1.5f, cy - h));
        ID2D1PathGeometry* triangle = TriangleGeometry();
        if (triangle) m_target->FillGeometry(triangle, brush);
    }
    m_target->FillRectangle(
        D2D1::RectF(cx - w * 1.5f - barWidth, cy - h, cx - w * 1.5f, cy + h), brush);
}

void Renderer::DrawPlayButton(D2D1_RECT_F rect, bool hovered, bool enabled) {
    if (!m_target) return;
    ID2D1SolidColorBrush* brush = DrawTransportButton(rect, hovered, enabled);

    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float h = (rect.bottom - rect.top) * 0.38f;
    const float w = (rect.right - rect.left) * 0.22f;

    // A right-pointing triangle's visual centre of mass sits a third of the way
    // along its length, so that is what gets aligned to the button centre.
    const ScopedTransform keep(m_target.Get());
    keep.Compose(D2D1::Matrix3x2F::Scale(w, 2.0f * h) *
                  D2D1::Matrix3x2F::Translation(cx - w / 3.0f, cy - h));
    ID2D1PathGeometry* triangle = TriangleGeometry();
    if (triangle) m_target->FillGeometry(triangle, brush);
}

void Renderer::DrawPauseButton(D2D1_RECT_F rect, bool hovered, bool enabled) {
    if (!m_target) return;
    ID2D1SolidColorBrush* brush = DrawTransportButton(rect, hovered, enabled);

    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float barWidth = (rect.right - rect.left) * 0.16f;
    const float barHeight = (rect.bottom - rect.top) * 0.42f;
    const float gap = barWidth * 0.9f;
    const float top = cy - barHeight / 2.0f;
    const float bottom = top + barHeight;

    m_target->FillRectangle(
        D2D1::RectF(cx - gap / 2.0f - barWidth, top, cx - gap / 2.0f, bottom), brush);
    m_target->FillRectangle(
        D2D1::RectF(cx + gap / 2.0f, top, cx + gap / 2.0f + barWidth, bottom), brush);
}

void Renderer::DrawSlider(D2D1_RECT_F track, float value01, float minValue01, float maxValue01,
                          bool active, bool enabled) {
    if (!m_target) return;
    value01 = std::clamp(value01, 0.0f, 1.0f);
    const float lo = std::clamp(minValue01, 0.0f, 1.0f);
    const float hi = std::clamp(maxValue01, lo, 1.0f);
    if (hi - lo <= 0.0f) return;

    const float cy = (track.top + track.bottom) / 2.0f;
    const float halfThickness = 2.5f;
    // A slider with a non-zero minimum (a monitor that refuses to go below 20%)
    // shows only the usable part of the track, so the thumb at the left end
    // really is the lowest value the hardware will accept.
    const float trackLeft = track.left + (track.right - track.left) * lo;
    const float trackRight = track.left + (track.right - track.left) * hi;
    const float thumbX = trackLeft + (trackRight - trackLeft) * value01;

    const D2D1_ROUNDED_RECT full = D2D1::RoundedRect(
        D2D1::RectF(trackLeft, cy - halfThickness, trackRight, cy + halfThickness), halfThickness,
        halfThickness);
    m_target->FillRoundedRectangle(full, m_controlBrush.Get());

    const D2D1_ROUNDED_RECT filled = D2D1::RoundedRect(
        D2D1::RectF(trackLeft, cy - halfThickness, thumbX, cy + halfThickness), halfThickness,
        halfThickness);
    m_target->FillRoundedRectangle(filled,
                                   enabled ? m_accentBrush.Get() : m_secondaryTextBrush.Get());

    const float radius = active ? 8.0f : 7.0f;
    m_target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), radius, radius),
                          enabled ? m_primaryTextBrush.Get() : m_secondaryTextBrush.Get());
}

void Renderer::DrawValueText(D2D1_RECT_F rect, const wchar_t* text, bool muted) {
    if (!m_target) return;
    m_target->DrawText(text, static_cast<UINT32>(wcslen(text)), ControlFormat(), rect,
                       muted ? m_secondaryTextBrush.Get() : m_primaryTextBrush.Get());
}

void Renderer::DrawProgress(D2D1_RECT_F rect, float value01, bool indeterminate) {
    if (!m_target) return;
    const float halfThickness = (rect.bottom - rect.top) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const D2D1_ROUNDED_RECT full = D2D1::RoundedRect(
        D2D1::RectF(rect.left, cy - halfThickness, rect.right, cy + halfThickness), halfThickness,
        halfThickness);
    m_target->FillRoundedRectangle(full, m_controlBrush.Get());

    // A live stream has no meaningful position, so the muted full-width bar is
    // all there is to say.
    if (indeterminate) return;

    value01 = std::clamp(value01, 0.0f, 1.0f);
    if (value01 <= 0.0f) return;
    const D2D1_ROUNDED_RECT filled = D2D1::RoundedRect(
        D2D1::RectF(rect.left, cy - halfThickness,
                    rect.left + (rect.right - rect.left) * value01, cy + halfThickness),
        halfThickness, halfThickness);
    m_target->FillRoundedRectangle(filled, m_accentBrush.Get());
}

void Renderer::DrawSpeaker(D2D1_RECT_F rect, ID2D1SolidColorBrush* brush) {
    ID2D1PathGeometry* body = SpeakerGeometry();
    if (!body) return;

    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const float scale = std::min(rect.right - rect.left, rect.bottom - rect.top) / 16.0f;
    const ScopedTransform keep(m_target.Get());
    keep.Compose(D2D1::Matrix3x2F::Scale(scale, scale) *
                  D2D1::Matrix3x2F::Translation(cx, cy));
    m_target->FillGeometry(body, brush);
}

void Renderer::DrawMuteButton(D2D1_RECT_F rect, bool muted, bool hovered) {
    if (!m_target) return;
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 5.0f, 5.0f);
    m_target->FillRoundedRectangle(rr, m_controlBrush.Get());
    if (hovered) m_target->FillRoundedRectangle(rr, m_hoverBrush.Get());

    ID2D1SolidColorBrush* brush = muted ? m_accentBrush.Get() : m_secondaryTextBrush.Get();
    DrawSpeaker(rect, brush);
    if (!muted) return;

    // The slash, drawn over the cone. Vectors again: U+1F508 exists only in
    // Segoe UI Emoji, so as a font glyph it would be a tofu box.
    ID2D1StrokeStyle* stroke = RoundStrokeStyle();
    if (!stroke) return;
    const float inset = (rect.right - rect.left) * 0.22f;
    m_target->DrawLine(D2D1::Point2F(rect.right - inset, rect.top + inset),
                       D2D1::Point2F(rect.left + inset, rect.bottom - inset), brush, 1.5f, stroke);
}

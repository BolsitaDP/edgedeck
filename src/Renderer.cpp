#include "Renderer.h"

using Microsoft::WRL::ComPtr;

namespace {

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

IDWriteTextFormat* TitleFormat() {
    if (!DWriteFactoryPtr()) return nullptr;
    static ComPtr<IDWriteTextFormat> fmt = [] {
        ComPtr<IDWriteTextFormat> f;
        DWriteFactoryPtr()->CreateTextFormat(
            L"Segoe UI Semibold", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us",
            f.GetAddressOf());
        if (f) f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return f;
    }();
    return fmt.Get();
}

IDWriteTextFormat* ItemFormat() {
    if (!DWriteFactoryPtr()) return nullptr;
    static ComPtr<IDWriteTextFormat> fmt = [] {
        ComPtr<IDWriteTextFormat> f;
        DWriteFactoryPtr()->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-us", f.GetAddressOf());
        if (f) f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return f;
    }();
    return fmt.Get();
}

IDWriteTextFormat* GlyphFormat() {
    if (!DWriteFactoryPtr()) return nullptr;
    static ComPtr<IDWriteTextFormat> fmt = [] {
        ComPtr<IDWriteTextFormat> f;
        DWriteFactoryPtr()->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", f.GetAddressOf());
        if (f) {
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return f;
    }();
    return fmt.Get();
}

IDWriteTextFormat* ControlFormat() {
    if (!DWriteFactoryPtr()) return nullptr;
    static ComPtr<IDWriteTextFormat> fmt = [] {
        ComPtr<IDWriteTextFormat> f;
        DWriteFactoryPtr()->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us",
            f.GetAddressOf());
        if (f) {
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return f;
    }();
    return fmt.Get();
}

// Push-pin silhouette (cap, stem, flared plate, needle) pointing down, in a
// box centered on the origin. Device-independent, so it is built once per
// process and shared by every panel's render target.
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

// Dark, Windows 11-ish palette. Colors only - no acrylic/Mica for this MVP.
const D2D1_COLOR_F kPanelBg = D2D1::ColorF(0.098f, 0.098f, 0.098f, 1.0f);
const D2D1_COLOR_F kTabBg = D2D1::ColorF(0.145f, 0.145f, 0.145f, 1.0f);
const D2D1_COLOR_F kTabHoverBg = D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f);
const D2D1_COLOR_F kRowHoverBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f);
const D2D1_COLOR_F kTextPrimary = D2D1::ColorF(0.93f, 0.93f, 0.93f, 1.0f);
const D2D1_COLOR_F kTextSecondary = D2D1::ColorF(0.65f, 0.65f, 0.65f, 1.0f);
const D2D1_COLOR_F kDivider = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f);
const D2D1_COLOR_F kControlBg = D2D1::ColorF(0.18f, 0.18f, 0.18f, 1.0f);
const D2D1_COLOR_F kAccent = D2D1::ColorF(0.30f, 0.62f, 0.98f, 1.0f);

} // namespace

Renderer::~Renderer() = default;

bool Renderer::AttachToWindow(HWND hwnd) {
    m_hwnd = hwnd;
    return EnsureTarget() && TitleFormat() && ItemFormat() && GlyphFormat() && ControlFormat();
}

bool Renderer::EnsureTarget() {
    if (m_target) return true;
    if (!m_hwnd) return false;

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT>(rc.right - rc.left),
                                    static_cast<UINT>(rc.bottom - rc.top));

    ID2D1Factory* factory = D2DFactory();
    if (!factory) return false;

    HRESULT hr = factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, size),
        m_target.ReleaseAndGetAddressOf());

    if (FAILED(hr)) return false;
    m_target->SetDpi(m_renderDpi, m_renderDpi);
    return true;
}

void Renderer::OnResize(UINT w, UINT h) {
    if (m_target) {
        if (m_target->Resize(D2D1::SizeU(w, h)) == D2DERR_RECREATE_TARGET) {
            DiscardTarget();
        }
    }
}

void Renderer::SetRenderDpi(float dpi) {
    m_renderDpi = dpi;
    if (m_target) m_target->SetDpi(dpi, dpi);
}

bool Renderer::EnsureBrush(ComPtr<ID2D1SolidColorBrush>& brush, D2D1_COLOR_F color) {
    if (brush) return true;
    return SUCCEEDED(m_target->CreateSolidColorBrush(color, brush.GetAddressOf()));
}

void Renderer::DiscardTarget() {
    m_primaryTextBrush.Reset();
    m_secondaryTextBrush.Reset();
    m_dividerBrush.Reset();
    m_hoverBrush.Reset();
    m_controlBrush.Reset();
    m_accentBrush.Reset();
    m_target.Reset();
}

void Renderer::DrawTab(bool hovered, float w, float h, float /*radius*/, const wchar_t* glyph) {
    if (!EnsureTarget()) return;
    if (!EnsureBrush(m_secondaryTextBrush, kTextSecondary)) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    t->BeginDraw();
    // Rounded corners come from the window region (SetWindowRgn); everything
    // painted here is a plain filled rect that gets clipped to that shape.
    t->Clear(hovered ? kTabHoverBg : kTabBg);

    D2D1_RECT_F textRect = D2D1::RectF(0, 0, w, h);
    t->DrawText(glyph, static_cast<UINT32>(wcslen(glyph)), GlyphFormat(), textRect,
                m_secondaryTextBrush.Get());

    HRESULT hr = t->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardTarget();
    }
}

void Renderer::DrawPanel(float w, float h, PanelWidget* widget, bool pinned, bool pinHovered) {
    if (!EnsureTarget()) return;
    if (!EnsureBrush(m_primaryTextBrush, kTextPrimary) ||
        !EnsureBrush(m_secondaryTextBrush, kTextSecondary) ||
        !EnsureBrush(m_dividerBrush, kDivider) ||
        !EnsureBrush(m_hoverBrush, kRowHoverBg) ||
        !EnsureBrush(m_controlBrush, kControlBg) ||
        !EnsureBrush(m_accentBrush, kAccent)) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    t->BeginDraw();
    t->SetTransform(D2D1::Matrix3x2F::Identity());
    t->Clear(kPanelBg);

    D2D1_RECT_F pinRect = PanelLayout::PinButtonRect(w);
    D2D1_RECT_F titleRect = D2D1::RectF(PanelLayout::PaddingX, 0.0f, pinRect.left - 6.0f,
                                         PanelLayout::ChromeHeight);
    const wchar_t* title = widget ? widget->PanelTitle() : L"EdgeDeck";
    t->DrawText(title, static_cast<UINT32>(wcslen(title)), TitleFormat(), titleRect,
                m_primaryTextBrush.Get());

    DrawPin(pinRect, pinned, pinHovered);

    t->DrawLine(D2D1::Point2F(PanelLayout::PaddingX, PanelLayout::ChromeHeight),
                D2D1::Point2F(w - PanelLayout::PaddingX, PanelLayout::ChromeHeight),
                m_dividerBrush.Get(), 1.0f);

    if (widget) {
        t->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, PanelLayout::ChromeHeight));
        widget->Draw(*this, w, h - PanelLayout::ChromeHeight - PanelLayout::BottomPadding);
        t->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    HRESULT hr = t->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardTarget();
    }
}

void Renderer::DrawPin(D2D1_RECT_F rect, bool pinned, bool hovered) {
    ID2D1HwndRenderTarget* t = m_target.Get();

    if (hovered) {
        t->FillRoundedRectangle(D2D1::RoundedRect(rect, 6.0f, 6.0f), m_hoverBrush.Get());
    }

    ID2D1PathGeometry* geometry = PinGeometry();
    if (!geometry) return;
    ID2D1StrokeStyle* stroke = RoundStrokeStyle();

    // Pinned: upright, solid accent color. Unpinned: tilted, outline only -
    // the classic "pin it" / "pinned" pair, readable at a glance.
    const float cx = (rect.left + rect.right) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    t->SetTransform(D2D1::Matrix3x2F::Rotation(pinned ? 0.0f : 40.0f) *
                    D2D1::Matrix3x2F::Translation(cx, cy));

    if (pinned) {
        t->FillGeometry(geometry, m_accentBrush.Get());
        t->DrawGeometry(geometry, m_accentBrush.Get(), 1.0f, stroke);
    } else {
        ID2D1SolidColorBrush* brush = hovered ? m_primaryTextBrush.Get() : m_secondaryTextBrush.Get();
        t->DrawGeometry(geometry, brush, 1.4f, stroke);
    }
    t->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Renderer::DrawRow(D2D1_RECT_F rect, const wchar_t* text, bool hovered) {
    if (!m_target) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    if (hovered) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(rect.left - 10.0f, rect.top + 2.0f, rect.right + 10.0f, rect.bottom - 2.0f),
            6.0f, 6.0f);
        t->FillRoundedRectangle(rr, m_hoverBrush.Get());
    }
    t->DrawText(text, static_cast<UINT32>(wcslen(text)), ItemFormat(), rect, m_primaryTextBrush.Get());
}

void Renderer::DrawLabel(D2D1_RECT_F rect, const wchar_t* text, bool muted) {
    if (!m_target) return;
    ID2D1HwndRenderTarget* t = m_target.Get();
    ID2D1SolidColorBrush* brush = muted ? m_secondaryTextBrush.Get() : m_primaryTextBrush.Get();
    t->DrawText(text, static_cast<UINT32>(wcslen(text)), ItemFormat(), rect, brush);
}

void Renderer::DrawBadge(D2D1_RECT_F rect, const wchar_t* letters, D2D1_COLOR_F color) {
    if (!m_target) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    // The color varies per app, so this brush can't come from the fixed
    // palette cache - creating a solid-color brush is cheap and only ever
    // happens during an actual WM_PAINT (event-driven, not per-frame).
    ComPtr<ID2D1SolidColorBrush> badgeBrush;
    if (FAILED(t->CreateSolidColorBrush(color, badgeBrush.GetAddressOf()))) return;

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 6.0f, 6.0f);
    t->FillRoundedRectangle(rr, badgeBrush.Get());
    t->DrawText(letters, static_cast<UINT32>(wcslen(letters)), GlyphFormat(), rect,
                m_primaryTextBrush.Get());
}

void Renderer::DrawIconButton(D2D1_RECT_F rect, const wchar_t* glyph, bool hovered, bool enabled) {
    if (!m_target) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 5.0f, 5.0f);
    t->FillRoundedRectangle(rr, m_controlBrush.Get());
    if (hovered && enabled) t->FillRoundedRectangle(rr, m_hoverBrush.Get());
    t->DrawText(glyph, static_cast<UINT32>(wcslen(glyph)), GlyphFormat(), rect,
                enabled ? m_primaryTextBrush.Get() : m_secondaryTextBrush.Get());
}

void Renderer::DrawPauseButton(D2D1_RECT_F rect, bool hovered, bool enabled) {
    if (!m_target) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, 5.0f, 5.0f);
    t->FillRoundedRectangle(rr, m_controlBrush.Get());
    if (hovered && enabled) t->FillRoundedRectangle(rr, m_hoverBrush.Get());

    ID2D1SolidColorBrush* brush =
        (enabled ? m_primaryTextBrush : m_secondaryTextBrush).Get();
    float barWidth = (rect.right - rect.left) * 0.16f;
    float barHeight = (rect.bottom - rect.top) * 0.42f;
    float gap = barWidth * 0.9f;
    float centerX = (rect.left + rect.right) / 2.0f;
    float top = (rect.top + rect.bottom) / 2.0f - barHeight / 2.0f;
    float bottom = top + barHeight;

    t->FillRectangle(D2D1::RectF(centerX - gap / 2.0f - barWidth, top, centerX - gap / 2.0f, bottom),
                      brush);
    t->FillRectangle(D2D1::RectF(centerX + gap / 2.0f, top, centerX + gap / 2.0f + barWidth, bottom),
                      brush);
}

void Renderer::DrawSlider(D2D1_RECT_F track, float value01, bool active, bool enabled) {
    if (!m_target) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    value01 = value01 < 0.0f ? 0.0f : (value01 > 1.0f ? 1.0f : value01);
    const float cy = (track.top + track.bottom) / 2.0f;
    const float halfThickness = 2.5f;
    const float thumbX = track.left + (track.right - track.left) * value01;

    D2D1_ROUNDED_RECT full = D2D1::RoundedRect(
        D2D1::RectF(track.left, cy - halfThickness, track.right, cy + halfThickness), halfThickness,
        halfThickness);
    t->FillRoundedRectangle(full, m_controlBrush.Get());

    D2D1_ROUNDED_RECT filled = D2D1::RoundedRect(
        D2D1::RectF(track.left, cy - halfThickness, thumbX, cy + halfThickness), halfThickness,
        halfThickness);
    t->FillRoundedRectangle(filled, enabled ? m_accentBrush.Get() : m_secondaryTextBrush.Get());

    const float radius = active ? 8.0f : 7.0f;
    t->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, cy), radius, radius),
                    enabled ? m_primaryTextBrush.Get() : m_secondaryTextBrush.Get());
}

void Renderer::DrawValueText(D2D1_RECT_F rect, const wchar_t* text, bool muted) {
    if (!m_target) return;
    ID2D1HwndRenderTarget* t = m_target.Get();
    t->DrawText(text, static_cast<UINT32>(wcslen(text)), ControlFormat(), rect,
                muted ? m_secondaryTextBrush.Get() : m_primaryTextBrush.Get());
}

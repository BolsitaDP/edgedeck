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

// Dark, Windows 11-ish palette. Colors only - no acrylic/Mica for this MVP.
const D2D1_COLOR_F kPanelBg = D2D1::ColorF(0.098f, 0.098f, 0.098f, 1.0f);
const D2D1_COLOR_F kTabBg = D2D1::ColorF(0.145f, 0.145f, 0.145f, 1.0f);
const D2D1_COLOR_F kTabHoverBg = D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f);
const D2D1_COLOR_F kRowHoverBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f);
const D2D1_COLOR_F kTextPrimary = D2D1::ColorF(0.93f, 0.93f, 0.93f, 1.0f);
const D2D1_COLOR_F kTextSecondary = D2D1::ColorF(0.65f, 0.65f, 0.65f, 1.0f);
const D2D1_COLOR_F kDivider = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f);

} // namespace

Renderer::~Renderer() = default;

bool Renderer::AttachToWindow(HWND hwnd) {
    m_hwnd = hwnd;
    return EnsureTarget() && TitleFormat() && ItemFormat() && GlyphFormat();
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

void Renderer::DrawPanel(float w, float /*h*/, float /*radius*/,
                          const std::vector<PanelItem>& items, int hoveredIndex) {
    if (!EnsureTarget()) return;
    if (!EnsureBrush(m_primaryTextBrush, kTextPrimary) ||
        !EnsureBrush(m_dividerBrush, kDivider) ||
        !EnsureBrush(m_hoverBrush, kRowHoverBg)) return;
    ID2D1HwndRenderTarget* t = m_target.Get();

    t->BeginDraw();
    t->Clear(kPanelBg);

    D2D1_RECT_F titleRect = D2D1::RectF(PanelLayout::PaddingX, 0.0f, w - PanelLayout::PaddingX,
                                         PanelLayout::TitleHeight);
    static constexpr wchar_t kTitle[] = L"EdgeDeck";
    t->DrawText(kTitle, static_cast<UINT32>(wcslen(kTitle)), TitleFormat(), titleRect,
                m_primaryTextBrush.Get());

    t->DrawLine(D2D1::Point2F(PanelLayout::PaddingX, PanelLayout::TitleHeight),
                D2D1::Point2F(w - PanelLayout::PaddingX, PanelLayout::TitleHeight),
                m_dividerBrush.Get(), 1.0f);

    for (size_t i = 0; i < items.size(); ++i) {
        float y = PanelLayout::TitleHeight + static_cast<float>(i) * PanelLayout::RowHeight;

        if (static_cast<int>(i) == hoveredIndex) {
            D2D1_ROUNDED_RECT hoverRect = D2D1::RoundedRect(
                D2D1::RectF(8.0f, y + 2.0f, w - 8.0f, y + PanelLayout::RowHeight - 2.0f), 6.0f,
                6.0f);
            t->FillRoundedRectangle(hoverRect, m_hoverBrush.Get());
        }

        D2D1_RECT_F rowRect =
            D2D1::RectF(PanelLayout::PaddingX, y, w - PanelLayout::PaddingX, y + PanelLayout::RowHeight);
        t->DrawText(items[i].text.c_str(), static_cast<UINT32>(items[i].text.size()), ItemFormat(),
                    rowRect, m_primaryTextBrush.Get());
    }

    HRESULT hr = t->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardTarget();
    }
}

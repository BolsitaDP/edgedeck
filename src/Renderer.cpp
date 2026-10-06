#include "Config.h"
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
IDWriteTextFormat* MakeFormat(float sizePx, DWRITE_FONT_WEIGHT weight, bool centered,
                              const wchar_t* family = L"Segoe UI") {
    if (!DWriteFactoryPtr()) return nullptr;
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(DWriteFactoryPtr()->CreateTextFormat(
            family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
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
// The system icon font, if there is one: Segoe Fluent Icons on Windows 11, Segoe MDL2
// Assets before it (and still alongside it). The tab icons are code points of these
// fonts. Looked up once; null when neither is installed, in which case tabs fall back
// to their plain text glyph rather than drawing boxes.
const wchar_t* IconFontFamily() {
    static const wchar_t* family = []() -> const wchar_t* {
        IDWriteFactory* factory = DWriteFactoryPtr();
        if (!factory) return nullptr;
        ComPtr<IDWriteFontCollection> fonts;
        if (FAILED(factory->GetSystemFontCollection(fonts.GetAddressOf(), FALSE))) return nullptr;
        for (const wchar_t* name : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists) return name;
        }
        return nullptr;
    }();
    return family;
}

IDWriteTextFormat* IconFormat() {
    static IDWriteTextFormat* fmt = [] {
        const wchar_t* family = IconFontFamily();
        return family ? MakeFormat(16.0f, DWRITE_FONT_WEIGHT_REGULAR, true, family) : nullptr;
    }();
    return fmt;
}

// A problem report in the header. The one text format that wraps: a title is a
// word or two, but a notice is a sentence, and two lines of it fit the header
// where one trimmed line would not say enough to be useful.
IDWriteTextFormat* NoticeFormat() {
    static IDWriteTextFormat* fmt = [] {
        IDWriteTextFormat* f = MakeFormat(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
        if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        return f;
    }();
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

namespace {

// The palette Windows itself uses, so the panel looks like part of the desktop
// rather than a foreign object pasted onto it.
//
// Windows 11's "Mica" and "Acrylic" materials blur whatever is behind the
// window, which a layered window with a 1-bit region cannot reproduce. What the
// system falls back to when the material is unavailable is a flat set of solid
// colours, and those are the values below - they are the same greys the
// Settings app and File Explorer land on with materials off, so a user who has
// turned materials off still sees their panel match the rest of Windows.
//
// Dark greys are the layered/elevated ramp (#202020, #2B2B2B, #333333); light
// uses the Mica-fallback neutral with white cards on top.
constexpr float kDarkPanel = 32.0f / 255.0f;   // #202020
constexpr float kDarkRail = 26.0f / 255.0f;    // #1A1A1A, one step below the panel
constexpr float kDarkControl = 51.0f / 255.0f; // #333333
constexpr float kDarkText = 255.0f / 255.0f;   // #FFFFFF
constexpr float kDarkTextSecondary = 197.0f / 255.0f; // #C5C5C5

constexpr float kLightPanel = 243.0f / 255.0f; // #F3F3F3, Mica fallback
constexpr float kLightRail = 236.0f / 255.0f;  // #ECECEC
constexpr float kLightControl = 225.0f / 255.0f; // #E1E1E1
constexpr float kLightText = 27.0f / 255.0f;   // #1B1B1B
constexpr float kLightTextSecondary = 94.0f / 255.0f; // #5E5E5E

// Relative luminance, for deciding whether the accent is readable on the panel.
float Luminance(D2D1_COLOR_F c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// Mixes towards white (or black) until the result clears `target` contrast
// against `against`.
//
// This matters because the accent is the user's choice and can be anything -
// including a dark blue that would be invisible as a filled slider on a dark
// panel. Windows lightens the accent for dark mode for exactly this reason; this
// is the same idea, computed rather than taken from a second registry value that
// is not always present.
D2D1_COLOR_F EnsureContrast(D2D1_COLOR_F accent, D2D1_COLOR_F against, float target) {
    // Contrast ratio between two relative luminances.
    auto ratio = [&](float a, float b) {
        const float hi = std::max(a, b);
        const float lo = std::min(a, b);
        return (hi + 0.05f) / (lo + 0.05f);
    };

    const float toward = Luminance(against) < 0.5f ? 1.0f : 0.0f;
    D2D1_COLOR_F result = accent;
    // 24 steps is enough to reach white from any starting point while changing
    // the hue as little as possible; each step moves 1/24 of the way.
    for (int i = 0; i < 24; ++i) {
        if (ratio(Luminance(result), Luminance(against)) >= target) break;
        const float step = static_cast<float>(i + 1) / 24.0f;
        result = D2D1::ColorF(accent.r + (toward - accent.r) * step,
                              accent.g + (toward - accent.g) * step,
                              accent.b + (toward - accent.b) * step, 1.0f);
    }
    return result;
}

// The user's accent colour, as Windows stores it: 0x00BBGGRR under
// Themes\\Personalize. Absent on some builds, in which case the modern default
// blue is a better guess than anything invented here.
D2D1_COLOR_F SystemAccent(bool dark) {
    DWORD bgr = 0;
    DWORD bytes = sizeof(bgr);
    const LSTATUS rc =
        RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AccentColor", RRF_RT_REG_DWORD, nullptr, &bgr, &bytes);

    if (rc != ERROR_SUCCESS) {
        return dark ? D2D1::ColorF(0.376f, 0.804f, 1.0f, 1.0f)    // #60CDFF
                    : D2D1::ColorF(0.0f, 0.475f, 0.831f, 1.0f);  // #0079D1
    }
    return D2D1::ColorF(static_cast<float>((bgr >> 16) & 0xFF) / 255.0f,
                        static_cast<float>((bgr >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(bgr & 0xFF) / 255.0f, 1.0f);
}

PanelTheme g_theme;
bool g_themeValid = false;

} // namespace

bool PanelTheme::operator==(const PanelTheme& other) const {
    auto same = [](D2D1_COLOR_F a, D2D1_COLOR_F b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    return highContrast == other.highContrast && dark == other.dark &&
           same(panelBg, other.panelBg) && same(tabBg, other.tabBg) &&
           same(tabHoverBg, other.tabHoverBg) && same(rowHoverBg, other.rowHoverBg) &&
           same(textPrimary, other.textPrimary) && same(textSecondary, other.textSecondary) &&
           same(divider, other.divider) && same(controlBg, other.controlBg) &&
           same(accent, other.accent) && same(edge, other.edge);
}

const PanelTheme& PanelTheme::Current() {
    if (!g_themeValid) Refresh();
    return g_theme;
}

bool PanelTheme::Refresh() {
    PanelTheme t;

    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    // The return value only says the call worked, which it always does; whether
    // high contrast is actually *on* is a bit in the structure it filled in.
    // Testing the return value instead pins the app permanently to the
    // GetSysColor() branch, which follows the light system colours and is why
    // the panel ignored the user's dark mode setting.
    t.highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != 0 &&
                     (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;

    // AppsUseLightTheme is the setting that governs how a classic Win32 app is
    // expected to look, which is exactly what this is.
    // pcbData must point at a real DWORD. Passing nullptr for it makes
    // RegGetValueW fail with ERROR_INVALID_PARAMETER, which - because the result
    // is a silent fallback to the light palette - meant this app had been
    // ignoring the user's dark mode setting entirely.
    DWORD appsUseLightTheme = 1;
    DWORD themeBytes = sizeof(appsUseLightTheme);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &appsUseLightTheme,
                     &themeBytes) != ERROR_SUCCESS) {
        appsUseLightTheme = 1;
    }
    // A preference of Dark or Light overrides what the system reports. High contrast
    // still wins over both, because honouring the user's chosen system colours is
    // the entire purpose of that mode.
    switch (Config::CurrentThemeMode()) {
        case ThemeMode::Dark: t.dark = true; break;
        case ThemeMode::Light: t.dark = false; break;
        default: t.dark = (appsUseLightTheme == 0); break;
    }

    if (t.highContrast) {
        // Whatever colours the user chose, honour them: use the system
        // window/button/text colours rather than inventing a palette of our own,
        // which is exactly what high contrast exists to prevent. Note that the
        // accent is the system highlight colour, not the personalisation one -
        // the whole point of the mode is that the user's choices win.
        t.panelBg = D2D1::ColorF(GetSysColor(COLOR_WINDOW), 1.0f);
        t.tabBg = D2D1::ColorF(GetSysColor(COLOR_BTNFACE), 1.0f);
        t.tabHoverBg = D2D1::ColorF(GetSysColor(COLOR_WINDOW), 1.0f);
        t.rowHoverBg = D2D1::ColorF(GetSysColor(COLOR_HIGHLIGHT), 1.0f);
        t.textPrimary = D2D1::ColorF(GetSysColor(COLOR_WINDOWTEXT), 1.0f);
        t.textSecondary = D2D1::ColorF(GetSysColor(COLOR_WINDOWTEXT), 1.0f);
        t.divider = D2D1::ColorF(GetSysColor(COLOR_WINDOWTEXT), 0.54f);
        t.controlBg = D2D1::ColorF(GetSysColor(COLOR_BTNFACE), 1.0f);
        t.accent = D2D1::ColorF(GetSysColor(COLOR_HIGHLIGHT), 1.0f);
        t.edge = D2D1::ColorF(GetSysColor(COLOR_WINDOWTEXT), 0.75f);
    } else if (t.dark) {
        t.panelBg = D2D1::ColorF(kDarkPanel, kDarkPanel, kDarkPanel, 1.0f);
        t.tabBg = D2D1::ColorF(kDarkRail, kDarkRail, kDarkRail, 1.0f);
        t.tabHoverBg = t.panelBg;
        t.rowHoverBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f);
        t.textPrimary = D2D1::ColorF(kDarkText, kDarkText, kDarkText, 1.0f);
        t.textSecondary = D2D1::ColorF(kDarkTextSecondary, kDarkTextSecondary, kDarkTextSecondary, 1.0f);
        t.divider = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f);
        t.controlBg = D2D1::ColorF(kDarkControl, kDarkControl, kDarkControl, 1.0f);
        t.accent = SystemAccent(true);
        t.edge = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f);
    } else {
        t.panelBg = D2D1::ColorF(kLightPanel, kLightPanel, kLightPanel, 1.0f);
        t.tabBg = D2D1::ColorF(kLightRail, kLightRail, kLightRail, 1.0f);
        t.tabHoverBg = t.panelBg;
        t.rowHoverBg = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f);
        t.textPrimary = D2D1::ColorF(kLightText, kLightText, kLightText, 1.0f);
        t.textSecondary =
            D2D1::ColorF(kLightTextSecondary, kLightTextSecondary, kLightTextSecondary, 1.0f);
        t.divider = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f);
        t.controlBg = D2D1::ColorF(kLightControl, kLightControl, kLightControl, 1.0f);
        t.accent = SystemAccent(false);
        t.edge = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.14f);
    }

    // The accent sits on the panel, on the slider track and on the progress bar.
    // 3:1 is the WCAG threshold for non-text UI, and is also roughly where a
    // filled shape stops reading as a shape.
    t.accent = EnsureContrast(t.accent, t.panelBg, 3.0f);

    const bool changed = !g_themeValid || !(g_theme == t);
    g_theme = t;
    g_themeValid = true;
    return changed;
}

// ---------------------------------------------------------------------------
// Surface lifecycle
// ---------------------------------------------------------------------------

Renderer::~Renderer() { DiscardTarget(); }

void Renderer::DiscardTarget() {
    ResetBrushes();
    m_target.Reset();
    // A layered surface goes with it. The DIB and its render target are cheap to
    // rebuild, and a half-valid one is not worth reasoning about.
    m_layered.reset();
    m_widthPx = 0;
    m_heightPx = 0;
}

bool Renderer::IconFontAvailable() { return IconFormat() != nullptr; }

void Renderer::ResetBrushes() {
    m_primaryTextBrush.Reset();
    m_secondaryTextBrush.Reset();
    m_dividerBrush.Reset();
    m_hoverBrush.Reset();
    m_controlBrush.Reset();
    m_accentBrush.Reset();
    m_edgeBrush.Reset();
}

bool Renderer::AttachToWindow(HWND hwnd, bool rounded) {
    m_hwnd = hwnd;
    m_rounded = rounded;
    // Decided once, here, because the choice also dictates how the window itself
    // is set up (no region, no SetLayeredWindowAttributes) and that has to agree
    // with the surface for as long as the window lives.
    m_layeredMode = LayeredTarget::Available(D2DFactory());
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

    // A target keeps the pixel size it was created with, so one left over from a
    // previous window size keeps drawing into the old dimensions and gets clipped
    // (or stretched). Resizing whenever the client rect no longer matches is what
    // keeps the two in step.
    if (m_target && m_widthPx == width && m_heightPx == height) return true;
    // Rebuilding on a size change is the mechanism working, not an event worth a
    // log line: a panel that resizes does this a few times, and the log is for
    // things that went wrong.
    if (m_target) DiscardTarget();

    ID2D1Factory* factory = D2DFactory();
    if (!factory) {
        Diagnostics::Error("Renderer: no Direct2D factory");
        return false;
    }

    if (m_layeredMode) {
        // No fallback to the region path from here: the window was set up for
        // per-pixel alpha (no region, no constant alpha), so an HWND target on it
        // would draw into a window that is never shown. Failing loudly is the
        // honest option, and Resize has already logged why.
        if (!m_layered) m_layered = std::make_unique<LayeredTarget>();
        if (!m_layered->Resize(factory, width, height)) return false;
        m_target = m_layered->Target();

        // Grayscale text. ClearType needs to know what is behind it, and a surface
        // with alpha does not - D2D would pick this on its own, but saying so keeps
        // the look from changing with whatever it decides.
        m_target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    } else {
        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> hwndTarget;
        const HRESULT hr = factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(width, height)),
            hwndTarget.GetAddressOf());
        if (FAILED(hr) || !hwndTarget) {
            Diagnostics::Error("Renderer: CreateHwndRenderTarget(%ux%u) failed: 0x%08lX", width,
                               height, static_cast<unsigned long>(hr));
            return false;
        }
        m_target = hwndTarget;
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

void Renderer::AdoptTheme() {
    const PanelTheme& latest = PanelTheme::Current();
    if (latest == m_theme) return;
    m_theme = latest;
    // The brushes bake in the old colours, so a palette change has to drop them
    // or the panel keeps painting with the previous theme's text and accents.
    ResetBrushes();
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
           make(m_controlBrush, m_theme.controlBg) && make(m_accentBrush, m_theme.accent) &&
           make(m_edgeBrush, m_theme.edge);
}

bool Renderer::Composite() {
    // The target presents itself; reaching into the HDC here would only defeat
    // D2D's own batching. What matters is that it exists and is sized.
    return m_target != nullptr && m_widthPx > 0 && m_heightPx > 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void Renderer::FillRoundedPanel(float w, float h, float radius, D2D1_COLOR_F color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(m_target->CreateSolidColorBrush(color, brush.GetAddressOf()))) return;
    m_target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, w, h), radius, radius),
                                   brush.Get());
}

void Renderer::PaintBackground(float w, float h, D2D1_COLOR_F color) {
    if (!m_layeredMode) {
        // The window region clips the surface to the rounded shape, so the surface
        // itself is a plain rectangle.
        m_target->Clear(color);
        return;
    }
    // Here the surface *is* the window shape. Clearing it to the (opaque) panel
    // colour would give every pixel an alpha of 255 and the corners would simply
    // never be transparent - transparent first, then the rounded fill on top.
    m_target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    FillRoundedPanel(w, h, m_cornerRadius, color);
}

void Renderer::DrawTab(bool hovered, float w, float h, const wchar_t* glyph) {
    if (!EnsureTarget()) return;

    // The palette itself is cached and refreshed from the theme window messages;
    // this only picks up a change that has already happened.
    AdoptTheme();
    if (!EnsureBrushes()) return;

    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    PaintBackground(w, h, hovered ? m_theme.tabHoverBg : m_theme.tabBg);
    DrawEdge(w, h);

    // Hovered, the tab is the same surface as the panel it opens, so the panel
    // reads as sliding out of the tab rather than appearing next to it.
    // A code point from the private-use block is an icon-font glyph; anything else
    // is plain text in the UI font.
    const bool isIcon = glyph[0] >= 0xE000 && glyph[0] <= 0xF8FF && IconFormat() != nullptr;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(m_target->CreateSolidColorBrush(
            hovered ? m_theme.textPrimary : m_theme.textSecondary, brush.GetAddressOf()))) {
        m_target->DrawText(glyph, static_cast<UINT32>(wcslen(glyph)),
                           isIcon ? IconFormat() : GlyphFormat(), D2D1::RectF(0.0f, 0.0f, w, h),
                           brush.Get());
    }

    if (m_target->EndDraw() == D2DERR_RECREATE_TARGET) {
        DiscardTarget();
        return;
    }
    PresentLayered();
}

// Forwards the rendered bitmap to the window. A no-op on the region path, where
// the target presents itself.
void Renderer::PresentLayered() {
    if (m_layered && m_target) m_layered->Present(m_hwnd, kSurfaceAlpha);
}

// The 1px inner outline, drawn inset by half a pixel so the stroke lands fully
// inside the window shape instead of straddling its edge. On the layered path it
// follows the rounded corners (the radius shrinks by the same half pixel, so the
// stroke stays concentric with the fill); on the region path the corners are cut
// by the region and a plain rectangle is what fits.
void Renderer::DrawEdge(float w, float h) {
    if (!m_edgeBrush || w <= 1.0f || h <= 1.0f) return;
    const D2D1_RECT_F inset = D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f);
    if (m_layeredMode) {
        const float radius = m_cornerRadius > 0.5f ? m_cornerRadius - 0.5f : 0.0f;
        m_target->DrawRoundedRectangle(D2D1::RoundedRect(inset, radius, radius), m_edgeBrush.Get(),
                                       1.0f);
        return;
    }
    m_target->DrawRectangle(inset, m_edgeBrush.Get(), 1.0f);
}

void Renderer::DrawPanel(float w, float h, PanelWidget* widget, bool pinned, bool pinHovered,
                         const wchar_t* notice) {
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

    AdoptTheme();
    if (!EnsureBrushes()) return;

    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    PaintBackground(w, h, m_theme.panelBg);
    DrawEdge(w, h);

    const D2D1_RECT_F pinRect = PanelLayout::PinButtonRect(w);
    const D2D1_RECT_F titleRect = D2D1::RectF(PanelLayout::PaddingX, 0.0f, pinRect.left - 6.0f,
                                              PanelLayout::ChromeHeight);
    const wchar_t* title = widget ? widget->PanelTitle() : L"EdgeDeck";

    if (notice && *notice) {
        // Amber on a dark panel, a darker amber on a light one - both clear 4.5:1
        // against their panel - and in high contrast the user's own text colour,
        // which is the only one guaranteed legible there. Clipped to the header so
        // a third line is cut off rather than drawn over the content below.
        const D2D1_COLOR_F warning = m_theme.highContrast ? m_theme.textPrimary
                                     : m_theme.dark       ? D2D1::ColorF(1.0f, 0.78f, 0.36f, 1.0f)
                                                          : D2D1::ColorF(0.62f, 0.34f, 0.0f, 1.0f);
        ComPtr<ID2D1SolidColorBrush> warningBrush;
        if (SUCCEEDED(m_target->CreateSolidColorBrush(warning, warningBrush.GetAddressOf()))) {
            m_target->DrawText(notice, static_cast<UINT32>(wcslen(notice)), NoticeFormat(), titleRect,
                               warningBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    } else {
        // The title is chrome, not content, so it is drawn in the secondary tone.
        // The rows below then carry the brightest text on the panel and the eye
        // lands on the data first, which is the whole point of the panel.
        m_target->DrawText(title, static_cast<UINT32>(wcslen(title)), TitleFormat(), titleRect,
                           m_secondaryTextBrush.Get());
    }

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

    if (m_target->EndDraw() == D2DERR_RECREATE_TARGET) {
        DiscardTarget();
        return;
    }
    PresentLayered();
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

    // The letter has to contrast with the *badge*, not with the panel. Borrowing
    // the panel's text brush left it unreadable on roughly half the possible
    // badge colours - white on a pale salmon badge, say - because the badge sits
    // at a mid lightness either way while the panel text is near-black or white.
    // Picking from the badge's own luminance is right for every hue, and it keeps
    // the known-app brand colours rendering the way their designers intended.
    const D2D1_COLOR_F letter =
        Luminance(color) > 0.45f ? D2D1::ColorF(0.07f, 0.07f, 0.07f, 1.0f)
                                 : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    ComPtr<ID2D1SolidColorBrush> letterBrush;
    if (FAILED(m_target->CreateSolidColorBrush(letter, letterBrush.GetAddressOf()))) return;

    m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, 6.0f, 6.0f), badgeBrush.Get());
    m_target->DrawText(letters, static_cast<UINT32>(wcslen(letters)), GlyphFormat(), rect,
                       letterBrush.Get());
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

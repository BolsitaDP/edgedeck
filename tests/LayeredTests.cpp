// Covers the claim the whole per-pixel-alpha path rests on: a software DC render
// target bound to a premultiplied DIB keeps its alpha channel. An earlier attempt
// concluded that every device-less Direct2D surface loses it, which is true of
// ID2D1HwndRenderTarget but not of this, and the difference is invisible until
// someone looks at the pixels - so this looks at the pixels. It needs no window:
// the DIB is the surface, and what UpdateLayeredWindow is later handed is exactly
// these bytes.
#include "TestHarness.h"

#include "LayeredTarget.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

ComPtr<ID2D1Factory> MakeFactory() {
    ComPtr<ID2D1Factory> factory;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    return factory;
}

BYTE AlphaAt(const LayeredTarget& surface, UINT x, UINT y) {
    return surface.Pixels()[(static_cast<size_t>(y) * surface.WidthPx() + x) * 4 + 3];
}

// Clears to transparent and fills a rounded rectangle, which is what the
// renderer's PaintBackground does on this path.
bool DrawRoundedPanel(LayeredTarget& surface, float radius) {
    ID2D1RenderTarget* target = surface.Target();
    if (!target) return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f),
                                             brush.GetAddressOf()))) {
        return false;
    }
    const float w = static_cast<float>(surface.WidthPx());
    const float h = static_cast<float>(surface.HeightPx());
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, w, h), radius, radius),
                                 brush.Get());
    return SUCCEEDED(target->EndDraw());
}

void TestCornersAreTransparent() {
    TEST("Layered: a rounded fill leaves transparent corners and an opaque centre");
    ComPtr<ID2D1Factory> factory = MakeFactory();
    CHECK(factory != nullptr);
    CHECK(LayeredTarget::Available(factory.Get()));

    LayeredTarget surface;
    CHECK(surface.Resize(factory.Get(), 120, 80));
    CHECK(DrawRoundedPanel(surface, 20.0f));

    CHECK_EQ(static_cast<int>(AlphaAt(surface, 0, 0)), 0);
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 119, 0)), 0);
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 0, 79)), 0);
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 119, 79)), 0);
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 60, 40)), 255);
    // Straight edges, away from the corners, are fully covered.
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 60, 0)), 255);
}

void TestCornerEdgeIsAntialiased() {
    TEST("Layered: the corner curve has partial alpha, not a hard staircase");
    ComPtr<ID2D1Factory> factory = MakeFactory();
    LayeredTarget surface;
    CHECK(surface.Resize(factory.Get(), 120, 80));
    CHECK(DrawRoundedPanel(surface, 20.0f));

    int partial = 0;
    for (UINT y = 0; y < 20; ++y) {
        for (UINT x = 0; x < 20; ++x) {
            const BYTE a = AlphaAt(surface, x, y);
            if (a > 0 && a < 255) ++partial;
        }
    }
    // A 1-bit mask would give none; a smooth radius-20 arc crosses a few dozen
    // pixels. The bound is loose on purpose - the point is "some", not "this many".
    CHECK(partial >= 8);
}

void TestPremultiplied() {
    TEST("Layered: colour channels are premultiplied by alpha");
    ComPtr<ID2D1Factory> factory = MakeFactory();
    LayeredTarget surface;
    CHECK(surface.Resize(factory.Get(), 120, 80));
    CHECK(DrawRoundedPanel(surface, 20.0f));

    // UpdateLayeredWindow's AC_SRC_ALPHA expects premultiplied pixels, so no
    // channel may ever exceed its own alpha. A straight-alpha surface would break
    // this on every antialiased pixel.
    int violations = 0;
    for (UINT y = 0; y < surface.HeightPx(); ++y) {
        for (UINT x = 0; x < surface.WidthPx(); ++x) {
            const BYTE* p = surface.Pixels() + (static_cast<size_t>(y) * surface.WidthPx() + x) * 4;
            if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) ++violations;
        }
    }
    CHECK_EQ(violations, 0);
}

void TestResizeKeepsWorking() {
    TEST("Layered: resizing rebuilds the surface at the new size and keeps its alpha");
    ComPtr<ID2D1Factory> factory = MakeFactory();
    LayeredTarget surface;
    CHECK(surface.Resize(factory.Get(), 120, 80));
    CHECK(DrawRoundedPanel(surface, 20.0f));

    // Same size is a no-op; a different one gets a new DIB and a fresh binding.
    CHECK(surface.Resize(factory.Get(), 120, 80));
    CHECK(surface.Resize(factory.Get(), 200, 150));
    CHECK_EQ(static_cast<int>(surface.WidthPx()), 200);
    CHECK_EQ(static_cast<int>(surface.HeightPx()), 150);
    CHECK(DrawRoundedPanel(surface, 20.0f));
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 0, 0)), 0);
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 100, 75)), 255);
}

void TestScalesWithDpi() {
    TEST("Layered: at 150% the corner is drawn in device pixels, still transparent and smooth");
    ComPtr<ID2D1Factory> factory = MakeFactory();
    LayeredTarget surface;
    // 180x120 device pixels is 120x80 logical units at 144 DPI.
    CHECK(surface.Resize(factory.Get(), 180, 120));
    ID2D1RenderTarget* target = surface.Target();
    CHECK(target != nullptr);
    if (!target) return;
    target->SetDpi(144.0f, 144.0f);

    ComPtr<ID2D1SolidColorBrush> brush;
    CHECK(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f),
                                                  brush.GetAddressOf())));
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, 120.0f, 80.0f), 10.0f, 10.0f),
                                 brush.Get());
    CHECK(SUCCEEDED(target->EndDraw()));

    // The logical rectangle covers the whole surface, and its radius of 10 is 15
    // device pixels: the corner pixel is empty, the pixel just inside the arc is
    // not, and the arc itself has partial alpha.
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 0, 0)), 0);
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 90, 60)), 255);
    CHECK_EQ(static_cast<int>(AlphaAt(surface, 179, 119)), 0);
    int partial = 0;
    for (UINT y = 0; y < 16; ++y) {
        for (UINT x = 0; x < 16; ++x) {
            const BYTE a = AlphaAt(surface, x, y);
            if (a > 0 && a < 255) ++partial;
        }
    }
    CHECK(partial >= 8);
}

void TestRejectsDegenerateSizes() {
    TEST("Layered: a zero-sized or factory-less request is refused, not half-built");
    ComPtr<ID2D1Factory> factory = MakeFactory();
    LayeredTarget surface;
    CHECK(!surface.Resize(factory.Get(), 0, 80));
    CHECK(!surface.Resize(factory.Get(), 120, 0));
    CHECK(!surface.Resize(nullptr, 120, 80));
    CHECK(surface.Target() == nullptr);
}

} // namespace

void RunLayeredTests() {
    TestCornersAreTransparent();
    TestCornerEdgeIsAntialiased();
    TestPremultiplied();
    TestResizeKeepsWorking();
    TestScalesWithDpi();
    TestRejectsDegenerateSizes();
}

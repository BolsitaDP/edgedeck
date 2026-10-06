#include "LayeredTarget.h"

#include "Diagnostics.h"

#include <cstring>

namespace {

// Premultiplied, because that is what UpdateLayeredWindow's AC_SRC_ALPHA expects:
// the colour channels are already scaled by alpha, so a half-covered corner pixel
// is "half as bright" rather than "full colour at half alpha".
D2D1_RENDER_TARGET_PROPERTIES SurfaceProperties() {
    return D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 0.0f, 0.0f,
        D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
}

} // namespace

bool LayeredTarget::Available(ID2D1Factory* factory) {
    static const bool available = [factory] {
        if (!factory) return false;
        const D2D1_RENDER_TARGET_PROPERTIES props = SurfaceProperties();
        Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> probe;
        const HRESULT hr = factory->CreateDCRenderTarget(&props, probe.GetAddressOf());
        if (FAILED(hr)) {
            Diagnostics::Error("Layered: no software DC render target (0x%08lX); using the window region",
                               static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }();
    return available;
}

LayeredTarget::~LayeredTarget() { Reset(); }

void LayeredTarget::Reset() {
    // The render target holds the DC, so it goes first; the DC then has to give
    // the DIB back before the DIB can be deleted.
    m_target.Reset();
    if (m_dc) {
        if (m_stockBitmap) SelectObject(m_dc, m_stockBitmap);
        DeleteDC(m_dc);
        m_dc = nullptr;
    }
    if (m_dib) {
        DeleteObject(m_dib);
        m_dib = nullptr;
    }
    m_stockBitmap = nullptr;
    m_bits = nullptr;
    m_widthPx = 0;
    m_heightPx = 0;
}

bool LayeredTarget::Resize(ID2D1Factory* factory, UINT widthPx, UINT heightPx) {
    if (!factory || widthPx == 0 || heightPx == 0) return false;
    if (m_target && m_dib && m_widthPx == widthPx && m_heightPx == heightPx) return true;

    if (!m_target) {
        const D2D1_RENDER_TARGET_PROPERTIES props = SurfaceProperties();
        const HRESULT hr = factory->CreateDCRenderTarget(&props, m_target.GetAddressOf());
        if (FAILED(hr)) {
            Diagnostics::Error("Layered: CreateDCRenderTarget failed: 0x%08lX",
                               static_cast<unsigned long>(hr));
            m_target.Reset();
            return false;
        }
    }

    if (!m_dc) {
        m_dc = CreateCompatibleDC(nullptr);
        if (!m_dc) {
            Diagnostics::Error("Layered: CreateCompatibleDC failed");
            return false;
        }
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(widthPx);
    info.bmiHeader.biHeight = -static_cast<LONG>(heightPx); // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(m_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        Diagnostics::Error("Layered: CreateDIBSection(%ux%u) failed", widthPx, heightPx);
        if (dib) DeleteObject(dib);
        return false;
    }

    // Select the new DIB in before the old one is deleted: a bitmap cannot be
    // deleted while it is selected into a DC.
    HGDIOBJ previous = SelectObject(m_dc, dib);
    if (!m_stockBitmap) m_stockBitmap = previous;
    if (m_dib) DeleteObject(m_dib);
    m_dib = dib;
    m_bits = bits;
    m_widthPx = widthPx;
    m_heightPx = heightPx;

    const RECT bounds{0, 0, static_cast<LONG>(widthPx), static_cast<LONG>(heightPx)};
    const HRESULT hr = m_target->BindDC(m_dc, &bounds);
    if (FAILED(hr)) {
        Diagnostics::Error("Layered: BindDC(%ux%u) failed: 0x%08lX", widthPx, heightPx,
                           static_cast<unsigned long>(hr));
        Reset();
        return false;
    }
    return true;
}

bool LayeredTarget::Present(HWND hwnd, BYTE constantAlpha) {
    if (!hwnd || !m_dc || !m_dib) return false;

    // pptDst is null, so the window keeps its position - the panel is slid by
    // SetWindowPos, and this must never fight that. The size is passed so the
    // window and its bitmap cannot drift apart after a resize.
    POINT src{0, 0};
    SIZE size{static_cast<LONG>(m_widthPx), static_cast<LONG>(m_heightPx)};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, constantAlpha, AC_SRC_ALPHA};

    if (!UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, m_dc, &src, 0, &blend, ULW_ALPHA)) {
        // A window being destroyed fails this legitimately; one line per target is
        // enough to notice a real problem without a line per paint.
        if (!m_loggedPresentFailure) {
            m_loggedPresentFailure = true;
            Diagnostics::Error("Layered: UpdateLayeredWindow failed: %s",
                               Diagnostics::LastErrorText().c_str());
        }
        return false;
    }
    return true;
}

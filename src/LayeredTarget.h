#pragma once

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

// Per-pixel-alpha rendering for one window.
//
// A window region (SetWindowRgn) is a 1-bit mask, so rounded corners cut with one
// are visibly staircased from 125% scaling up. Antialiased corners need real
// alpha, and this is where it comes from: Direct2D draws into a 32-bit
// premultiplied DIB, and UpdateLayeredWindow hands that DIB to the compositor.
//
// Software rendering, deliberately. An ID2D1DCRenderTarget of type SOFTWARE
// rasterises on the CPU straight into the DIB, which means no Direct3D device, no
// GPU driver pulled into the process, and no discrete GPU woken for a 300x200
// panel - the cost of this app being resident matters more here than the
// throughput, and a panel this small is a fraction of a millisecond to draw.
// (A DXGI-backed device context also keeps alpha, but it needs D3D11, a read-back
// out of video memory, and all of the above.) The earlier claim that every
// device-less D2D target loses its alpha was about ID2D1HwndRenderTarget, which
// composites through GDI; a DC render target bound to a DIB does not.
//
// UpdateLayeredWindow and SetLayeredWindowAttributes are mutually exclusive, and
// a window region clips the alpha away again. A window on this path must have
// neither - see Tab's ApplyCornerTreatment.
class LayeredTarget {
public:
    LayeredTarget() = default;
    ~LayeredTarget();

    LayeredTarget(const LayeredTarget&) = delete;
    LayeredTarget& operator=(const LayeredTarget&) = delete;

    // True when a software DC render target can be created at all. Probed once
    // with the caller's factory and cached; false sends the caller back to the
    // region path, which needs nothing from here.
    static bool Available(ID2D1Factory* factory);

    // (Re)creates the DIB at this size and binds the render target to it. The
    // render target itself is kept across resizes. `factory` must be the one the
    // rest of the caller's Direct2D resources (geometries, stroke styles) came
    // from: Direct2D refuses to mix resources from two factories.
    bool Resize(ID2D1Factory* factory, UINT widthPx, UINT heightPx);

    // The target to draw into, or null before Resize has succeeded.
    ID2D1RenderTarget* Target() const { return m_target.Get(); }

    // Pushes the DIB to the window. constantAlpha scales the per-pixel alpha, so
    // 255 leaves the drawing as is and anything lower makes the whole window
    // uniformly see-through on top of the antialiased shape.
    bool Present(HWND hwnd, BYTE constantAlpha);

    // Top-down premultiplied BGRA, widthPx * 4 bytes per row. Exposed so the unit
    // tests can check that the corners really are transparent.
    const BYTE* Pixels() const { return static_cast<const BYTE*>(m_bits); }
    UINT WidthPx() const { return m_widthPx; }
    UINT HeightPx() const { return m_heightPx; }

    void Reset();

private:
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_target;
    HDC m_dc = nullptr;
    HBITMAP m_dib = nullptr;
    HGDIOBJ m_stockBitmap = nullptr; // what the DC held before our DIB went in
    void* m_bits = nullptr;
    UINT m_widthPx = 0;
    UINT m_heightPx = 0;
    bool m_loggedPresentFailure = false;
};

#pragma once

#include <windows.h>
#include <d2d1.h>

// What kind of content a tab's panel shows. Used by the settings UI to offer
// a type picker and by Config to persist/restore which widget a tab has.
enum class WidgetType {
    QuickActions,
    Media,
};

// Drawing primitives a PanelWidget can call without owning any D2D
// resources itself - Renderer implements this and keeps brush caching
// centralized in one place instead of duplicated per widget.
class IPanelPainter {
public:
    virtual ~IPanelPainter() = default;

    // rect is in the widget's own local content coordinates (0,0 = top-left
    // of the content area, i.e. already below the panel's title/pin chrome).
    virtual void DrawRow(D2D1_RECT_F rect, const wchar_t* text, bool hovered) = 0;

    // Plain text, no background/hover fill. muted = secondary (dimmer) color.
    virtual void DrawLabel(D2D1_RECT_F rect, const wchar_t* text, bool muted) = 0;

    // Small colored rounded-square badge with 1-2 centered letters - the
    // "icon" for a row that isn't backed by a real extracted app icon.
    virtual void DrawBadge(D2D1_RECT_F rect, const wchar_t* letters, D2D1_COLOR_F color) = 0;

    // Small square button with a centered symbol glyph; dimmed when disabled.
    virtual void DrawIconButton(D2D1_RECT_F rect, const wchar_t* glyph, bool hovered,
                                 bool enabled) = 0;

    // Same button chrome as DrawIconButton, but draws a pause icon (two
    // bars) as vector shapes instead of a font glyph - U+23F8 isn't covered
    // by the fonts available here and renders as a broken box.
    virtual void DrawPauseButton(D2D1_RECT_F rect, bool hovered, bool enabled) = 0;
};

// One tab hosts exactly one PanelWidget. This is deliberately 1:1 (not a
// list of widgets per panel) - simpler hit-testing, simpler settings UI
// ("pick a widget type for this tab"), and matches the product shape
// (each concern gets its own tab). A composite widget could always be added
// later as just another PanelWidget implementation if that ever changes.
class PanelWidget {
public:
    virtual ~PanelWidget() = default;

    virtual WidgetType Type() const = 0;

    // Single character shown on the small edge tab. Fixed per widget type -
    // it identifies *which* tab this is, independent of open/closed state
    // (the slide animation already communicates state).
    virtual const wchar_t* TabGlyph() const = 0;

    // Header text drawn in the panel's chrome.
    virtual const wchar_t* PanelTitle() const = 0;

    // Logical (96-DPI) height this widget wants for its content area, given
    // the panel's logical width. Drives how tall the panel window is made.
    virtual float PreferredContentHeight(float logicalWidth) const = 0;

    // Draw content into [0,0]-[width,contentHeight] local coordinates.
    virtual void Draw(IPanelPainter& painter, float width, float contentHeight) = 0;

    // Hit-test a point in the same local coordinate space. Returns a
    // widget-defined control id, or -1 for no hit.
    virtual int HitTest(float x, float y, float width, float contentHeight) const = 0;

    virtual void SetHovered(int controlId) = 0;

    // ownerHwnd is the tab's own HWND - widgets that need to post an async
    // result back to themselves (see OnAsyncResult) target this handle.
    virtual void Activate(int controlId, HWND ownerHwnd) = 0;

    // Default no-op; only widgets with async work (e.g. MediaWidget)
    // override this. message/wParam come straight from the tab's WndProc.
    virtual void OnAsyncResult(UINT /*message*/, WPARAM /*wParam*/) {}

    // Called right when the panel begins opening (hover or otherwise). Lets
    // a widget kick off a per-open refresh (e.g. MediaWidget re-scanning
    // active sessions) instead of polling in the background. Default no-op.
    virtual void OnPanelOpening(HWND /*ownerHwnd*/) {}
};

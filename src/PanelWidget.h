#pragma once

#include <windows.h>
#include <d2d1.h>

#include <string>

// What kind of content a tab's panel shows. Used by the settings UI to offer
// a type picker and by Config to persist/restore which widget a tab has.
enum class WidgetType {
    QuickActions,
    Media,
    Brightness,
    Lyrics,
    Volume,
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
    // Long text is trimmed with an ellipsis rather than clipped mid-glyph.
    virtual void DrawLabel(D2D1_RECT_F rect, const wchar_t* text, bool muted) = 0;

    // Two-line text: a bold primary line and a dimmer secondary line, used for
    // rows that carry both a name and a detail value.
    virtual void DrawLabelPair(D2D1_RECT_F rect, const wchar_t* primary, const wchar_t* secondary) = 0;

    // Small colored rounded-square badge with 1-2 centered letters - the
    // "icon" for a row that isn't backed by a real extracted app icon.
    virtual void DrawBadge(D2D1_RECT_F rect, const wchar_t* letters, D2D1_COLOR_F color) = 0;

    // Transport controls, drawn as vector shapes. Segoe UI - the only font the
    // renderer names - has no glyph for U+23EE (previous), U+23ED (next) or
    // U+23F8 (pause), so all three are built from primitives; a play triangle
    // happens to render correctly as U+25B6 but is drawn the same way for
    // consistency of weight and alignment.
    virtual void DrawPrevButton(D2D1_RECT_F rect, bool hovered, bool enabled) = 0;
    virtual void DrawPlayButton(D2D1_RECT_F rect, bool hovered, bool enabled) = 0;
    virtual void DrawPauseButton(D2D1_RECT_F rect, bool hovered, bool enabled) = 0;
    virtual void DrawNextButton(D2D1_RECT_F rect, bool hovered, bool enabled) = 0;

    // Horizontal slider: `track` is the full track span (thumb travels along
    // its horizontal extent, vertically centered), value01 in [0,1]. `active`
    // = hovered or being dragged (thumb grows slightly).
    virtual void DrawSlider(D2D1_RECT_F track, float value01, float minValue01, float maxValue01,
                            bool active, bool enabled) = 0;

    // Short centered value text (e.g. "75%") next to a slider.
    virtual void DrawValueText(D2D1_RECT_F rect, const wchar_t* text, bool muted) = 0;

    // Thin rounded progress bar (playback position), value01 clamped to [0,1].
    // `indeterminate` draws the muted full-width bar for a stream of unknown
    // length.
    virtual void DrawProgress(D2D1_RECT_F rect, float value01, bool indeterminate) = 0;

    // Speaker with a slash through it, drawn as vectors. U+1F507/U+1F508 only
    // exist in Segoe UI Emoji, so a glyph here would render as a tofu box for
    // the same reason U+23EE did.
    virtual void DrawMuteButton(D2D1_RECT_F rect, bool muted, bool hovered) = 0;
};

// One tab hosts exactly one PanelWidget. This is deliberately 1:1 (not a
// list of widgets per panel) - simpler hit-testing, simpler settings UI
// ("pick a widget type for this tab"), and matches the product shape
// (each concern gets its own tab). A composite widget could always be added
// later as just another PanelWidget implementation if that ever changes.
class PanelWidget {
public:
    virtual ~PanelWidget() = default;

    // Every widget that reports async work lives in this private WM_APP range.
    // Tab forwards only messages inside it, so an unrelated WM_APP+ message
    // posted to a tab window some day is not silently swallowed.
    static constexpr UINT kWidgetMessageFirst = WM_APP + 1;
    static constexpr UINT kWidgetMessageLast = WM_APP + 199;
    static bool IsWidgetMessage(UINT message) {
        return message >= kWidgetMessageFirst && message <= kWidgetMessageLast;
    }

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

    // Narrowest logical width at which this widget is still usable. Settings
    // clamps the panel width to this (and never below), so a slider, a
    // transport button row and a readable name can coexist instead of
    // degenerating into a 38-pixel name field.
    virtual float MinContentWidth() const = 0;

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
    // override this. message/wParam come straight from the tab's WndProc, and
    // wParam is always an AsyncEnvelope* that the widget owns and must delete
    // whether or not it recognises it.
    virtual void OnAsyncResult(UINT /*message*/, WPARAM /*wParam*/) {}

    // Called right when the panel begins opening (hover or otherwise). Lets
    // a widget kick off a per-open refresh (e.g. MediaWidget re-scanning
    // active sessions) instead of polling in the background. Default no-op.
    virtual void OnPanelOpening(HWND /*ownerHwnd*/) {}

    // Called when the panel finishes opening, and again when it is about to be
    // hidden. Lets a widget start and stop whatever it needs for the duration
    // of the panel being on screen - a widget that follows playback position
    // runs its timer here, so nothing ticks while the panel is closed.
    virtual void OnPanelVisibilityChanged(bool /*visible*/) {}

    // True if the widget needs OnTick while its panel is visible. Tab only runs
    // the tick timer when a panel is actually open, so an idle EdgeDeck still
    // has no timers at all.
    virtual bool WantsTicks() const { return false; }
    virtual void OnTick() {}

    // Text an assistive technology can announce for the panel as a whole, and
    // for one control id. Defaults describe the title only; widgets with real
    // content override them.
    virtual std::wstring AccessibleSummary() const { return PanelTitle(); }
    virtual std::wstring AccessibleControlText(int /*controlId*/) const { return {}; }

    // Drag support for sliders. Coordinates are content-local logical units
    // (same space as HitTest). OnDragBegin returns true to claim the press:
    // Tab then captures the mouse and routes moves to OnDragMove until the
    // button is released (OnDragEnd). Defaults: no dragging.
    virtual bool OnDragBegin(float /*x*/, float /*y*/, float /*width*/, float /*contentHeight*/,
                              HWND /*ownerHwnd*/) { return false; }
    virtual void OnDragMove(float /*x*/, float /*y*/, float /*width*/, float /*contentHeight*/) {}
    virtual void OnDragEnd() {}

    // Keyboard operation. The panel is WS_EX_NOACTIVATE and never takes focus
    // from the foreground app, so it drives its own focus with the mouse
    // wheel, arrow keys or a number key while the pointer is over it. These
    // four hooks keep that logic in the widget, next to the hit-test it has to
    // agree with.
    virtual int FocusableControlCount() const { return 0; }
    virtual int FocusedControl() const { return -1; }
    virtual void SetFocusedControl(int /*controlId*/) {}
    // Returns true if the key was consumed.
    virtual bool OnKeyDown(UINT /*key*/, int /*focusedControl*/) { return false; }
    // Left/right step for a focused slider. Returns the new value in [0,1].
    virtual bool OnStepControl(int /*controlId*/, int /*direction*/, float& /*outValue01*/) {
        return false;
    }
};

#include "DialogTheme.h"

#include "Renderer.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cwchar>

namespace {

COLORREF ToColorRef(const D2D1_COLOR_F& c) {
    auto channel = [](float v) {
        return static_cast<BYTE>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return RGB(channel(c.r), channel(c.g), channel(c.b));
}

// Buttons, lists and the trackbar take the Explorer style; combo boxes and edit boxes take the
// file-dialog one.
BOOL CALLBACK ThemeChild(HWND child, LPARAM dark) {
    wchar_t cls[32];
    if (!GetClassNameW(child, cls, 32)) return TRUE;

    const wchar_t* darkTheme = nullptr;
    if (_wcsicmp(cls, L"Button") == 0 || _wcsicmp(cls, L"ListBox") == 0 ||
        _wcsicmp(cls, TRACKBAR_CLASSW) == 0) {
        darkTheme = L"DarkMode_Explorer";
    } else if (_wcsicmp(cls, L"ComboBox") == 0 || _wcsicmp(cls, L"Edit") == 0) {
        darkTheme = L"DarkMode_CFD";
    }
    // Null for both names puts the control back on the default style, which is
    // what a window that has just been switched back to light needs.
    if (darkTheme) SetWindowTheme(child, dark ? darkTheme : nullptr, nullptr);
    return TRUE;
}

} // namespace

void DialogTheme::Release() {
    if (m_windowBrush) DeleteObject(m_windowBrush);
    if (m_controlBrush) DeleteObject(m_controlBrush);
    m_windowBrush = nullptr;
    m_controlBrush = nullptr;
    m_themed = false;
}

void DialogTheme::Apply(HWND window) {
    if (!window) return;

    const PanelTheme& theme = PanelTheme::Current();
    const bool dark = theme.dark && !theme.highContrast;

    Release();
    m_themed = dark;
    if (m_themed) {
        m_windowColor = ToColorRef(theme.panelBg);
        m_controlColor = ToColorRef(theme.controlBg);
        m_textColor = ToColorRef(theme.textPrimary);
        m_mutedColor = ToColorRef(theme.textSecondary);
        m_windowBrush = CreateSolidBrush(m_windowColor);
        m_controlBrush = CreateSolidBrush(m_controlColor);
    }

    // The title bar is drawn by the window manager, not by us. Attribute 20 is
    // the documented one (Windows 10 2004 and later); 19 is what it was called in
    // the builds before that, and failing both is harmless - the bar stays light.
    const BOOL useDark = m_themed ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark)))) {
        DwmSetWindowAttribute(window, 19, &useDark, sizeof(useDark));
    }

    EnumChildWindows(window, ThemeChild, m_themed ? 1 : 0);
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

bool DialogTheme::EraseBackground(HWND window, HDC dc) const {
    if (!Themed()) return false;
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, m_windowBrush);
    return true;
}

LRESULT DialogTheme::ControlColor(UINT message, HDC dc, HWND control, bool alsoField) const {
    const bool field = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || alsoField;

    SetTextColor(dc, IsWindowEnabled(control) ? m_textColor : m_mutedColor);
    SetBkColor(dc, field ? m_controlColor : m_windowColor);
    return reinterpret_cast<LRESULT>(field ? m_controlBrush : m_windowBrush);
}

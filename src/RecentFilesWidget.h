#pragma once

#include "AsyncResult.h"
#include "PanelWidget.h"
#include "RecentFiles.h"

#include <memory>
#include <string>
#include <vector>

// The newest files of one folder - Downloads unless the settings name another - one row each with
// its name and how long ago it was written; a click opens the file, and a last row opens the
// folder. The folder is listed each time the panel opens, on a short-lived thread, and not watched.
class RecentFilesWidget : public PanelWidget {
public:
    RecentFilesWidget();

    WidgetType Type() const override { return WidgetType::RecentFiles; }
    const wchar_t* TabGlyph() const override;
    const wchar_t* TabIcon() const override;
    const wchar_t* PanelTitle() const override;

    float PreferredContentHeight(float logicalWidth) const override;
    float MinContentWidth() const override { return 240.0f; }
    void Draw(IPanelPainter& painter, float width, float contentHeight) override;
    int HitTest(float x, float y, float width, float contentHeight) const override;
    void SetHovered(int controlId) override { m_hovered = controlId; }
    void Activate(int controlId, HWND ownerHwnd) override;
    void OnPanelOpening(HWND ownerHwnd) override;
    void OnSettingsChanged() override;
    void OnAsyncResult(UINT message, WPARAM wParam) override;

    std::wstring AccessibleSummary() const override;
    std::wstring AccessibleControlText(int controlId) const override;

    int FocusableControlCount() const override { return ControlCount(); }
    int FocusedControl() const override { return m_focused; }
    void SetFocusedControl(int controlId) override { m_focused = controlId; }
    bool OnKeyDown(UINT key, int focusedControl) override;

private:
    static constexpr UINT kListingMessage = kWidgetMessageFirst + 61;

    struct Row {
        RecentFiles::Entry entry;
        std::wstring age;
        std::shared_ptr<const PanelImage> image; // only for a file with an icon of its own
    };

    // The folder to list: the setting, or Downloads when it is empty.
    std::wstring Folder() const;
    void Request();
    // Files, then the "open folder" row when the folder exists.
    int ControlCount() const;
    float BlockHeight() const;

    std::vector<Row> m_rows;
    std::wstring m_folder;     // what the rows above are the listing of
    bool m_loaded = false;     // a listing has arrived at least once
    bool m_folderFound = false;
    std::shared_ptr<const PanelImage> m_folderImage;
    RequestTracker m_requests;
    int m_hovered = -1;
    int m_focused = -1;
    HWND m_owner = nullptr;
};

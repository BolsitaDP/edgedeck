#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

// Lets files dragged from Explorer be dropped on a window.
//
// One registration covers the whole window: OLE looks for a drop target on the window under the
// pointer and, finding none there, on its parent, so a drop on a list, an edit box or a button
// reaches the window's target. (DragAcceptFiles, the older way, does not do that for child
// controls.) tools/scripts/Test-ActionDrop.ps1 drops on each kind of control.
//
// Only while the window exists: nothing is registered at startup, nothing runs for a window that
// is not open.
namespace FileDrop {

class Zone {
public:
    using Callback = std::function<void(std::vector<std::wstring>)>;

    Zone() = default;
    ~Zone() { Detach(); }
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;

    // Makes `window` accept dropped files. `onFiles` is called on the UI thread with the full
    // paths that were dropped. False if OLE could not be started on this thread.
    bool Attach(HWND window, Callback onFiles);

    // Gives the window back. Safe to call twice, and from the window's WM_DESTROY.
    void Detach();

private:
    class Target;
    Target* m_target = nullptr;
    HWND m_window = nullptr;
    bool m_oleStarted = false;
};

} // namespace FileDrop

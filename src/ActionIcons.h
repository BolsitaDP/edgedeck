#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "AsyncResult.h"
#include "PanelImage.h"

// The pictures on the Quick Actions rows: the icon of the program a row starts, or the folder it
// opens - read straight out of the file, the way the shell itself would, but without the shell.
//
// That last part is the point. Asking Explorer's machinery for an icon (SHGetFileInfo, the system
// image list, IShellLink) is the obvious route and costs the process about 8 MB, ~100 handles and
// two threads the first time, none of which it ever gives back; reading the icon resource of the
// file costs a third of a megabyte. EdgeDeck's whole reason to exist is to sit at the edge of the
// screen using next to nothing, so the picture of a row is whatever can be had that way, and a row
// that cannot - a document, a web address, a Store app - shows a lettered badge instead.
//
// Nothing here runs on the UI thread: a file on a share that is not answering would hold the
// panel. The widget asks once, when its panel first opens, and a short-lived thread answers with
// one posted message. Nothing runs while the panel is closed, and nothing is resident.
namespace ActionIcons {

// An icon drawn once over black and once over white says exactly how see-through each pixel is:
// over black a pixel shows colour * alpha (which is the premultiplied colour), over white the same
// plus whatever of the white the alpha let through, so alpha = 255 - (white - black). It works for
// every kind of icon - 32-bit with an alpha channel, or an old one that is a colour image with a
// mask - because GDI has already put the mask and the alpha together by the time it draws.
// `out` receives pixelCount premultiplied BGRA pixels. A pointer may not be null.
void SeparateFromBackgrounds(const std::uint8_t* onBlack, const std::uint8_t* onWhite,
                             std::size_t pixelCount, std::uint8_t* out);

// Reads an icon as width x height premultiplied pixels. Null if the icon or the size is unusable.
std::shared_ptr<const PanelImage> ImageFromIcon(HICON icon, int width, int height);

// What a shortcut (.lnk) file says about its picture and its target - the parts of the format
// (MS-SHLLINK) that are needed to draw a row, read without COM. Strings are as stored: a
// %Variable% is still a variable, and a relative path is relative to the shortcut's folder.
struct ShortcutInfo {
    std::wstring iconPath;   // the file the shortcut says its icon is in, if it says
    int iconIndex = 0;       // and which icon there (negative: a resource id)
    std::wstring targetPath; // what it starts: from the link's own record, else its stored
                             // environment-variable form, else the relative path
    bool targetIsRelative = false;
};

// Parses the bytes of a .lnk. False if they are not one, or are cut short; never reads outside
// `bytes`. A file that is merely odd yields whatever was found before the oddity.
bool ReadShortcut(const std::uint8_t* bytes, std::size_t size, ShortcutInfo& info);

// The picture for what a row opens, `target` already having had its %Variables% expanded. Null
// when there is no honest picture to show: the built-in "show desktop", a file that is not there,
// a document or a web address (their icon belongs to whatever program is associated with them,
// which is the shell's to say, and is not worth what asking costs). The caller draws a badge.
std::shared_ptr<const PanelImage> ImageForTarget(const std::wstring& target);

// The answer to LoadAsync: one picture, or none, per target asked about, in the order asked.
struct IconBatch : AsyncEnvelope {
    AsyncKind Kind() const override { return AsyncKind::ActionIcons; }
    std::vector<std::wstring> targets;
    std::vector<std::shared_ptr<const PanelImage>> images;
};

// Looks the targets up on a thread of its own and posts an IconBatch to notifyWindow as
// notifyMessage, stamped with requestId. Ownership of the batch follows PostOrDelete: a window
// that is gone by then simply never sees it.
void LoadAsync(std::vector<std::wstring> targets, HWND notifyWindow, UINT notifyMessage,
               std::uint64_t requestId);

} // namespace ActionIcons

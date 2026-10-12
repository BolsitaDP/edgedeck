#include "ActionIcons.h"

#include "Diagnostics.h"
#include "QuickAction.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <thread>

namespace ActionIcons {
namespace {

bool StartsWithNoCase(const std::wstring& text, const wchar_t* prefix) {
    const size_t length = wcslen(prefix);
    return text.size() >= length && _wcsnicmp(text.c_str(), prefix, length) == 0;
}

// The extension of a path, lower-cased, with its dot ("" when there is none).
std::wstring ExtensionOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    std::wstring extension = path.substr(dot);
    for (wchar_t& c : extension) c = static_cast<wchar_t>(towlower(c));
    return extension;
}

// Files whose icon is a resource in the file itself.
bool CarriesItsOwnIcons(const std::wstring& path) {
    const std::wstring extension = ExtensionOf(path);
    return extension == L".exe" || extension == L".dll" || extension == L".ico" ||
           extension == L".icl" || extension == L".scr" || extension == L".cpl";
}

std::wstring Expanded(const std::wstring& text) {
    if (text.find(L'%') == std::wstring::npos) return text;
    wchar_t buffer[MAX_PATH * 2];
    const DWORD length = ExpandEnvironmentStringsW(text.c_str(), buffer, MAX_PATH * 2);
    if (length == 0 || length > MAX_PATH * 2) return text;
    return buffer;
}

// An icon of the file, near `pixels` square. A negative index is a resource id, as in a
// shortcut's icon location.
std::shared_ptr<const PanelImage> ExtractIcon(const std::wstring& file, int index, int pixels) {
    HICON icon = nullptr;
    UINT id = 0;
    const UINT found = PrivateExtractIconsW(file.c_str(), index, pixels, pixels, &icon, &id, 1, 0);
    if (found == 0 || found == 0xFFFFFFFF || !icon) return nullptr;
    auto image = ImageFromIcon(icon, pixels, pixels);
    DestroyIcon(icon);
    return image;
}

constexpr int kIconPixels = 48;

// A bounds-checked reader over the bytes of a shortcut. Every read says whether it fit.
class Reader {
public:
    Reader(const std::uint8_t* bytes, std::size_t size) : m_bytes(bytes), m_size(size) {}

    bool U16(std::size_t at, std::uint32_t& out) const {
        if (at > m_size || m_size - at < 2) return false;
        out = m_bytes[at] | (m_bytes[at + 1] << 8);
        return true;
    }
    bool U32(std::size_t at, std::uint32_t& out) const {
        if (at > m_size || m_size - at < 4) return false;
        out = m_bytes[at] | (m_bytes[at + 1] << 8) | (m_bytes[at + 2] << 16) |
              (static_cast<std::uint32_t>(m_bytes[at + 3]) << 24);
        return true;
    }
    // `count` UTF-16 characters, or ANSI bytes widened. A string that runs off the end is refused.
    bool Text(std::size_t at, std::size_t count, bool unicode, std::wstring& out) const {
        const std::size_t width = unicode ? 2 : 1;
        if (count > m_size || at > m_size || (m_size - at) / width < count) return false;
        out.clear();
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint8_t* p = m_bytes + at + i * width;
            out.push_back(unicode ? static_cast<wchar_t>(p[0] | (p[1] << 8)) : static_cast<wchar_t>(p[0]));
        }
        return true;
    }
    // A zero-terminated string starting at `at`, no longer than `limit` characters.
    bool Terminated(std::size_t at, bool unicode, std::wstring& out, std::size_t limit = 1040) const {
        const std::size_t width = unicode ? 2 : 1;
        out.clear();
        for (std::size_t i = 0; i < limit; ++i) {
            const std::size_t position = at + i * width;
            if (position > m_size || m_size - position < width) return false;
            const std::uint8_t* p = m_bytes + position;
            const wchar_t c = unicode ? static_cast<wchar_t>(p[0] | (p[1] << 8)) : static_cast<wchar_t>(p[0]);
            if (c == 0) return true;
            out.push_back(c);
        }
        return false;
    }
    std::size_t size() const { return m_size; }

private:
    const std::uint8_t* m_bytes;
    std::size_t m_size;
};

std::shared_ptr<const PanelImage> ImageForPath(const std::wstring& path, int depth);

// A shortcut's picture: the icon location it names if it names one, else the picture of what it
// starts.
std::shared_ptr<const PanelImage> ImageForShortcut(const std::wstring& path, int depth) {
    if (depth > 0) return nullptr; // a shortcut to a shortcut: not worth following
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER size{};
    std::vector<std::uint8_t> bytes;
    // A shortcut is a few hundred bytes to a few kilobytes; anything past this is not one.
    constexpr LONGLONG kLargest = 256 * 1024;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= kLargest) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) read = 0;
        bytes.resize(read);
    }
    CloseHandle(file);

    ShortcutInfo info;
    if (bytes.empty() || !ReadShortcut(bytes.data(), bytes.size(), info)) return nullptr;

    if (!info.iconPath.empty()) {
        const std::wstring icon = Expanded(info.iconPath);
        if (CarriesItsOwnIcons(icon) && GetFileAttributesW(icon.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (auto image = ExtractIcon(icon, info.iconIndex, kIconPixels)) return image;
        }
    }

    if (info.targetPath.empty()) return nullptr;
    std::wstring target = Expanded(info.targetPath);
    if (info.targetIsRelative) {
        // Relative to the folder the shortcut is in.
        const size_t slash = path.find_last_of(L"\\/");
        const std::wstring folder = slash == std::wstring::npos ? L"." : path.substr(0, slash);
        wchar_t full[MAX_PATH * 2];
        const DWORD length = GetFullPathNameW((folder + L"\\" + target).c_str(), MAX_PATH * 2, full, nullptr);
        if (length == 0 || length >= MAX_PATH * 2) return nullptr;
        target = full;
    }
    return ImageForPath(target, depth + 1);
}

// The picture for a path that is known to be a thing on disk or not: a folder, a program or icon
// file, a shortcut to one of those - and nothing for the rest.
std::shared_ptr<const PanelImage> ImageForPath(const std::wstring& path, int depth) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return nullptr;

    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        // The folder icon is a resource of imageres.dll, id 3, on every Windows since Vista;
        // SHGetStockIconInfo says the same thing but loads a good part of the shell to say it.
        return ExtractIcon(Expanded(L"%SystemRoot%\\System32\\imageres.dll"), -3, kIconPixels);
    }
    if (ExtensionOf(path) == L".lnk") return ImageForShortcut(path, depth);
    if (CarriesItsOwnIcons(path)) return ExtractIcon(path, 0, kIconPixels);
    return nullptr;
}

} // namespace

void SeparateFromBackgrounds(const std::uint8_t* onBlack, const std::uint8_t* onWhite,
                             std::size_t pixelCount, std::uint8_t* out) {
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::uint8_t* b = onBlack + i * 4;
        const std::uint8_t* w = onWhite + i * 4;
        // Each colour channel carries the same 255 - alpha; the average of the three shrugs off
        // the one-step rounding GDI leaves in each.
        int missing = 0;
        for (int c = 0; c < 3; ++c) missing += std::max(0, static_cast<int>(w[c]) - static_cast<int>(b[c]));
        missing = (missing + 1) / 3;
        const int alpha = std::clamp(255 - missing, 0, 255);

        std::uint8_t* o = out + i * 4;
        for (int c = 0; c < 3; ++c) {
            // A premultiplied colour is never brighter than its alpha.
            o[c] = static_cast<std::uint8_t>(std::min<int>(b[c], alpha));
        }
        o[3] = static_cast<std::uint8_t>(alpha);
    }
}

std::shared_ptr<const PanelImage> ImageFromIcon(HICON icon, int width, int height) {
    if (!icon || width <= 0 || height <= 0 || width > 512 || height > 512) return nullptr;

    // One 32-bit surface, drawn on twice.
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!dc) return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(dc);
        return nullptr;
    }
    HGDIOBJ previous = SelectObject(dc, bitmap);

    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    std::vector<std::uint8_t> onBlack(bytes);
    std::vector<std::uint8_t> onWhite(bytes);
    auto* pixels = static_cast<std::uint8_t*>(bits);

    // All zero: black, and an alpha of zero as well, so nothing here can be mistaken for opaque.
    std::fill_n(pixels, bytes, std::uint8_t{0x00});
    bool ok = DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL) != FALSE;
    GdiFlush();
    std::copy_n(pixels, bytes, onBlack.begin());

    std::fill_n(pixels, bytes, std::uint8_t{0xFF});
    ok = ok && DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL) != FALSE;
    GdiFlush();
    std::copy_n(pixels, bytes, onWhite.begin());

    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    if (!ok) return nullptr;

    auto image = std::make_shared<PanelImage>();
    image->id = NextImageId();
    image->width = width;
    image->height = height;
    image->bgra.resize(bytes);
    SeparateFromBackgrounds(onBlack.data(), onWhite.data(), static_cast<size_t>(width) * height,
                            image->bgra.data());
    return image;
}

bool ReadShortcut(const std::uint8_t* bytes, std::size_t size, ShortcutInfo& info) {
    info = ShortcutInfo{};
    if (!bytes) return false;
    const Reader in(bytes, size);

    // The header: its own size (always 0x4C) and the class id every shortcut carries.
    std::uint32_t headerSize = 0;
    if (!in.U32(0, headerSize) || headerSize != 0x4C) return false;
    static const std::uint8_t kClassId[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                                              0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    if (size < 20 || std::memcmp(bytes + 4, kClassId, sizeof(kClassId)) != 0) return false;

    std::uint32_t flags = 0;
    std::uint32_t iconIndex = 0;
    if (!in.U32(20, flags) || !in.U32(56, iconIndex)) return false;
    constexpr std::uint32_t kHasIdList = 1u << 0;
    constexpr std::uint32_t kHasLinkInfo = 1u << 1;
    constexpr std::uint32_t kHasName = 1u << 2;
    constexpr std::uint32_t kHasRelativePath = 1u << 3;
    constexpr std::uint32_t kHasWorkingDir = 1u << 4;
    constexpr std::uint32_t kHasArguments = 1u << 5;
    constexpr std::uint32_t kHasIconLocation = 1u << 6;
    constexpr std::uint32_t kIsUnicode = 1u << 7;
    const bool unicode = (flags & kIsUnicode) != 0;
    info.iconIndex = static_cast<int>(iconIndex);

    std::size_t at = headerSize;

    if (flags & kHasIdList) {
        std::uint32_t idListSize = 0;
        if (!in.U16(at, idListSize)) return true; // the header was good; nothing more is known
        at += 2;
        if (idListSize > size - std::min(size, at)) return true;
        at += idListSize;
    }

    // The link's own record of where it points: a base path and a suffix.
    std::wstring localBase;
    std::wstring suffix;
    if (flags & kHasLinkInfo) {
        const std::size_t start = at;
        std::uint32_t linkInfoSize = 0, headerLength = 0, infoFlags = 0, baseOffset = 0, suffixOffset = 0;
        if (!in.U32(start, linkInfoSize) || !in.U32(start + 4, headerLength) ||
            !in.U32(start + 8, infoFlags) || !in.U32(start + 16, baseOffset) ||
            !in.U32(start + 24, suffixOffset) || linkInfoSize < 0x1C || linkInfoSize > size - start) {
            return true;
        }
        constexpr std::uint32_t kLocalBasePath = 1u << 0;
        // From header length 0x24 the same strings are also stored as UTF-16, which is the
        // better copy where it is there.
        std::uint32_t baseOffsetUnicode = 0, suffixOffsetUnicode = 0;
        const bool hasUnicodeOffsets =
            headerLength >= 0x24 && in.U32(start + 28, baseOffsetUnicode) &&
            in.U32(start + 32, suffixOffsetUnicode);
        if (infoFlags & kLocalBasePath) {
            if (hasUnicodeOffsets && baseOffsetUnicode != 0) {
                in.Terminated(start + baseOffsetUnicode, true, localBase);
            } else if (baseOffset != 0) {
                in.Terminated(start + baseOffset, false, localBase);
            }
            if (hasUnicodeOffsets && suffixOffsetUnicode != 0) {
                in.Terminated(start + suffixOffsetUnicode, true, suffix);
            } else if (suffixOffset != 0) {
                in.Terminated(start + suffixOffset, false, suffix);
            }
        }
        at = start + linkInfoSize;
    }

    // The string data: each is a count, then that many characters.
    auto readString = [&](std::wstring& out) -> bool {
        std::uint32_t count = 0;
        if (!in.U16(at, count)) return false;
        at += 2;
        if (!in.Text(at, count, unicode, out)) return false;
        at += static_cast<std::size_t>(count) * (unicode ? 2 : 1);
        return true;
    };
    std::wstring scratch;
    std::wstring relative;
    bool intact = true;
    if (intact && (flags & kHasName)) intact = readString(scratch);
    if (intact && (flags & kHasRelativePath)) intact = readString(relative);
    if (intact && (flags & kHasWorkingDir)) intact = readString(scratch);
    if (intact && (flags & kHasArguments)) intact = readString(scratch);
    if (intact && (flags & kHasIconLocation)) intact = readString(info.iconPath);

    // The extra blocks: the one that matters here is the target spelled with environment
    // variables (a shortcut into %ProgramFiles% or %windir%), which has no record in LinkInfo.
    std::wstring environmentTarget;
    if (intact) {
        std::size_t block = at;
        for (int guard = 0; guard < 32; ++guard) {
            std::uint32_t blockSize = 0, signature = 0;
            if (!in.U32(block, blockSize) || blockSize < 8) break;
            if (!in.U32(block + 4, signature) || blockSize > size - std::min(size, block)) break;
            constexpr std::uint32_t kEnvironmentBlock = 0xA0000001u;
            // Size, signature, then 260 ANSI bytes and 260 UTF-16 characters of the target.
            if (signature == kEnvironmentBlock && blockSize >= 8 + 260 + 520) {
                in.Terminated(block + 8 + 260, true, environmentTarget, 260);
            }
            block += blockSize;
        }
    }

    if (!localBase.empty()) {
        info.targetPath = localBase + suffix;
    } else if (!environmentTarget.empty()) {
        info.targetPath = environmentTarget;
    } else if (!relative.empty()) {
        info.targetPath = relative;
        info.targetIsRelative = true;
    }
    return true;
}

std::shared_ptr<const PanelImage> ImageForTarget(const std::wstring& target) {
    if (target.empty() || target == QuickActions::kShowDesktop) return nullptr;
    // A web address or another scheme has no file to read a picture from.
    if (target.find(L"://") != std::wstring::npos || StartsWithNoCase(target, L"mailto:")) return nullptr;

    std::wstring path = target;
    // A bare name ("calc.exe") is whatever the search path finds, as when it is run.
    if (path.find_first_of(L"\\/:") == std::wstring::npos) {
        wchar_t found[MAX_PATH];
        const DWORD length = SearchPathW(nullptr, path.c_str(), L".exe", MAX_PATH, found, nullptr);
        if (length == 0 || length >= MAX_PATH) return nullptr;
        path.assign(found, length);
    }
    return ImageForPath(path, 0);
}

void LoadAsync(std::vector<std::wstring> targets, HWND notifyWindow, UINT notifyMessage,
               std::uint64_t requestId) {
    auto* batch = new IconBatch();
    batch->requestId = requestId;
    batch->targets = std::move(targets);
    batch->images.resize(batch->targets.size()); // all "none" until a lookup says otherwise

    try {
        std::thread([batch, notifyWindow, notifyMessage] {
            for (size_t i = 0; i < batch->targets.size(); ++i) {
                try {
                    batch->images[i] = ImageForTarget(batch->targets[i]);
                } catch (...) {
                    // One target that throws is a row without a picture, not a lost batch.
                }
            }
            PostOrDelete(notifyWindow, notifyMessage, batch);
        }).detach();
    } catch (...) {
        // No thread to be had: report "no pictures" so the rows settle on their letter badges
        // instead of waiting for an answer that is never coming.
        Diagnostics::Error("ActionIcons: could not start the lookup thread");
        PostOrDelete(notifyWindow, notifyMessage, batch);
    }
}

} // namespace ActionIcons

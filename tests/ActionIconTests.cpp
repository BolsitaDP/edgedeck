// The pictures on the Quick Actions rows: how an icon is turned into pixels (all the arithmetic and
// both kinds of icon GDI knows), and what the shell is asked for. Whether a picture then reaches
// the screen is tools/scripts/Test-QuickActions.ps1.
#include "TestHarness.h"

#include "ActionIcons.h"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

struct Bgra {
    int b, g, r, a;
};

Bgra PixelOf(const PanelImage& image, int x, int y) {
    const std::uint8_t* p = image.bgra.data() + (static_cast<size_t>(y) * image.width + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

bool Near(int actual, int expected, int tolerance = 2) {
    return actual >= expected - tolerance && actual <= expected + tolerance;
}

bool Matches(const Bgra& got, int b, int g, int r, int a, int tolerance = 2) {
    return Near(got.b, b, tolerance) && Near(got.g, g, tolerance) && Near(got.r, r, tolerance) &&
           Near(got.a, a, tolerance);
}

// "b,g,r,a" - what a failed pixel check prints, so a miss says what the pixel was.
std::string Describe(const Bgra& p) {
    char text[48];
    std::snprintf(text, sizeof(text), "%d,%d,%d,%d", p.b, p.g, p.r, p.a);
    return text;
}

// A pixel check that reports the pixel it found.
#define CHECK_PIXEL(image, x, y, b, g, r, a)                                              \
    do {                                                                                  \
        const Bgra got_ = PixelOf((image), (x), (y));                                     \
        ++::testing::g_checks;                                                            \
        if (!Matches(got_, (b), (g), (r), (a))) {                                         \
            ::testing::ReportFailure(__FILE__, __LINE__,                                  \
                                     "pixel (" #x "," #y ") is " + Describe(got_) +       \
                                         ", expected " #b "," #g "," #r "," #a);          \
        }                                                                                 \
    } while (0)

// A 32-bit top-down DIB section of the given size, pixels left to the caller to fill.
HBITMAP MakeDib(int width, int height, std::uint8_t** bits) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* raw = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
    ReleaseDC(nullptr, screen);
    *bits = static_cast<std::uint8_t*>(raw);
    return dib;
}

void SetPixel(std::uint8_t* bits, int width, int x, int y, int b, int g, int r, int a) {
    std::uint8_t* p = bits + (static_cast<size_t>(y) * width + x) * 4;
    p[0] = static_cast<std::uint8_t>(b);
    p[1] = static_cast<std::uint8_t>(g);
    p[2] = static_cast<std::uint8_t>(r);
    p[3] = static_cast<std::uint8_t>(a);
}

void TestBackgroundsSeparateIntoPremultipliedPixels() {
    TEST("ActionIcons: a pixel over black and over white gives back its alpha and premultiplied colour");
    // Order is B, G, R, A in every pixel. Over white, a pixel shows its premultiplied colour plus
    // the (255 - alpha) of white it let through, in every channel.
    const std::uint8_t black[] = {
        50, 100, 200, 0,  // opaque
        0, 0, 0, 0,       // transparent
        25, 50, 100, 0,   // half-transparent, colour already multiplied by its alpha
    };
    const std::uint8_t white[] = {
        50, 100, 200, 255,
        255, 255, 255, 255,
        25 + 127, 50 + 127, 100 + 127, 255,
    };
    std::uint8_t out[12] = {};
    ActionIcons::SeparateFromBackgrounds(black, white, 3, out);

    CHECK(out[0] == 50 && out[1] == 100 && out[2] == 200 && out[3] == 255);
    CHECK(out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0);
    CHECK(out[8] == 25 && out[9] == 50 && out[10] == 100 && Near(out[11], 128, 1));
}

void TestAnIconWithAnAlphaChannelIsReadBack() {
    TEST("ActionIcons: a 32-bit icon with per-pixel alpha comes back as the same premultiplied pixels");
    constexpr int kSize = 4;
    std::uint8_t* colour = nullptr;
    HBITMAP colourBitmap = MakeDib(kSize, kSize, &colour);
    CHECK(colourBitmap != nullptr);
    if (!colourBitmap) return;

    // Left half opaque, right half 50%, top-right corner fully clear. The colours are given the
    // way an .ico file holds them - not multiplied by their alpha - and what comes back is the
    // premultiplied form Direct2D wants, so the 50% pixel's (40,60,20) is read back as (20,30,10).
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            if (x < 2) SetPixel(colour, kSize, x, y, 10, 200, 100, 255);
            else if (x == 3 && y == 0) SetPixel(colour, kSize, x, y, 0, 0, 0, 0);
            else SetPixel(colour, kSize, x, y, 40, 60, 20, 128);
        }
    }
    HBITMAP mask = CreateBitmap(kSize, kSize, 1, 1, nullptr); // all zero: nothing masked out
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = colourBitmap;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(colourBitmap);
    DeleteObject(mask);
    CHECK(icon != nullptr);
    if (!icon) return;

    const auto image = ActionIcons::ImageFromIcon(icon, kSize, kSize);
    DestroyIcon(icon);
    CHECK(image != nullptr);
    if (!image) return;
    CHECK(image->Valid());
    CHECK_EQ(image->width, kSize);
    CHECK(image->id != 0);
    CHECK_PIXEL(*image, 0, 0, 10, 200, 100, 255);
    CHECK_PIXEL(*image, 1, 3, 10, 200, 100, 255);
    CHECK_PIXEL(*image, 2, 1, 20, 30, 10, 128);
    CHECK_PIXEL(*image, 3, 0, 0, 0, 0, 0);
}

void TestAnOldMaskedIconIsReadBack() {
    TEST("ActionIcons: an icon that is a colour image plus a mask (no alpha) keeps its shape");
    constexpr int kSize = 4;
    std::uint8_t* colour = nullptr;
    HBITMAP colourBitmap = MakeDib(kSize, kSize, &colour);
    CHECK(colourBitmap != nullptr);
    if (!colourBitmap) return;
    // Alpha bytes all zero is how an image with no alpha channel looks to GDI: the mask decides.
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) SetPixel(colour, kSize, x, y, x < 2 ? 200 : 0, x < 2 ? 50 : 0,
                                                 x < 2 ? 30 : 0, 0);
    }
    // 1 bit per pixel, rows padded to 16 bits; a set bit is "see through". Left two pixels solid.
    const std::uint8_t maskBits[8] = {0x30, 0, 0x30, 0, 0x30, 0, 0x30, 0};
    HBITMAP mask = CreateBitmap(kSize, kSize, 1, 1, maskBits);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = colourBitmap;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(colourBitmap);
    DeleteObject(mask);
    CHECK(icon != nullptr);
    if (!icon) return;

    const auto image = ActionIcons::ImageFromIcon(icon, kSize, kSize);
    DestroyIcon(icon);
    CHECK(image != nullptr);
    if (!image) return;
    CHECK_PIXEL(*image, 0, 0, 200, 50, 30, 255);
    CHECK_PIXEL(*image, 1, 2, 200, 50, 30, 255);
    CHECK_PIXEL(*image, 2, 0, 0, 0, 0, 0);
    CHECK_PIXEL(*image, 3, 3, 0, 0, 0, 0);
}

void TestUnusableIconsGiveNothing() {
    TEST("ActionIcons: no icon, or an impossible size, is no picture rather than a crash");
    CHECK(ActionIcons::ImageFromIcon(nullptr, 32, 32) == nullptr);
    HICON app = LoadIconW(nullptr, IDI_APPLICATION);
    CHECK(app != nullptr);
    CHECK(ActionIcons::ImageFromIcon(app, 0, 32) == nullptr);
    CHECK(ActionIcons::ImageFromIcon(app, 32, -1) == nullptr);
    CHECK(ActionIcons::ImageFromIcon(app, 100000, 100000) == nullptr);
    const auto fine = ActionIcons::ImageFromIcon(app, 32, 32);
    CHECK(fine != nullptr && fine->Valid());
}

// How much of an image is not see-through: a picture that came back all clear is a failure that
// would otherwise pass for "there is an image".
int OpaquePixels(const PanelImage& image) {
    int count = 0;
    for (size_t i = 3; i < image.bgra.size(); i += 4) count += image.bgra[i] > 200 ? 1 : 0;
    return count;
}

std::wstring SystemRoot() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"SystemRoot", buffer, MAX_PATH);
    return length > 0 && length < MAX_PATH ? std::wstring(buffer, length) : L"C:\\Windows";
}

void TestPicturesComeOnlyFromThingsThatCarryOne() {
    TEST("ActionIcons: a program and a folder have pictures; a document, an address or nothing invents none");
    const auto program = ActionIcons::ImageForTarget(SystemRoot() + L"\\explorer.exe");
    CHECK(program != nullptr);
    if (program) {
        CHECK(program->Valid());
        CHECK(OpaquePixels(*program) > program->width * program->height / 10);
    }

    const auto folder = ActionIcons::ImageForTarget(SystemRoot());
    CHECK(folder != nullptr && folder->Valid());

    // Found the way running it would find it.
    const auto bare = ActionIcons::ImageForTarget(L"notepad.exe");
    CHECK(bare != nullptr && bare->Valid());

    // Two asks are two pictures: the id is what the painter keys its bitmap by.
    if (program && folder) CHECK(program->id != folder->id);

    CHECK(ActionIcons::ImageForTarget(L"") == nullptr);
    CHECK(ActionIcons::ImageForTarget(L"builtin:desktop") == nullptr);
    CHECK(ActionIcons::ImageForTarget(L"C:\\no\\such\\folder\\edgedeck-missing.exe") == nullptr);
    CHECK(ActionIcons::ImageForTarget(L"no-such-program-edgedeck.exe") == nullptr);
    CHECK(ActionIcons::ImageForTarget(L"https://example.org/") == nullptr);
    CHECK(ActionIcons::ImageForTarget(L"ms-settings:display") == nullptr);
    CHECK(ActionIcons::ImageForTarget(L"steam://run/1") == nullptr);

    // A document's picture belongs to whatever opens it, which only the shell can say, and asking
    // costs the process several megabytes: it gets the lettered badge, and this is where that is
    // decided.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring document = std::wstring(temp) + L"edgedeck-icon-test.txt";
    HANDLE file = CreateFileW(document.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    CHECK(ActionIcons::ImageForTarget(document) == nullptr);
    DeleteFileW(document.c_str());
}

// A shortcut written by the shell's own writer, so that what is parsed is what Windows makes.
bool MakeShortcut(const std::wstring& file, const std::wstring& target, const std::wstring& iconFile,
                  int iconIndex) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        !link) {
        return false;
    }
    link->SetPath(target.c_str());
    if (!iconFile.empty()) link->SetIconLocation(iconFile.c_str(), iconIndex);
    IPersistFile* persist = nullptr;
    bool saved = false;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist))) && persist) {
        saved = SUCCEEDED(persist->Save(file.c_str(), TRUE));
        persist->Release();
    }
    link->Release();
    return saved;
}

std::vector<std::uint8_t> ReadAll(const std::wstring& file) {
    std::vector<std::uint8_t> bytes;
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return bytes;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20)) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
        bytes.resize(read);
    }
    CloseHandle(h);
    return bytes;
}

bool SameText(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

void TestShortcutsAreReadByHand() {
    TEST("ActionIcons: a shortcut's icon location and target are read from the file, with no COM");
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp);
    const std::wstring explorer = SystemRoot() + L"\\explorer.exe";
    const std::wstring shell32 = SystemRoot() + L"\\System32\\shell32.dll";

    const std::wstring plain = dir + L"edgedeck-lnk-plain.lnk";
    const std::wstring withIcon = dir + L"edgedeck-lnk-icon.lnk";
    const std::wstring toFolder = dir + L"edgedeck-lnk-folder.lnk";
    const std::wstring toNothing = dir + L"edgedeck-lnk-missing.lnk";
    CHECK(MakeShortcut(plain, explorer, L"", 0));
    CHECK(MakeShortcut(withIcon, explorer, shell32, 21));
    CHECK(MakeShortcut(toFolder, SystemRoot(), L"", 0));
    // The shell resolves a target when it writes the link, so a missing one is made from a file
    // that exists and then deleted.
    const std::wstring doomed = dir + L"edgedeck-lnk-doomed.exe";
    CopyFileW(explorer.c_str(), doomed.c_str(), FALSE);
    CHECK(MakeShortcut(toNothing, doomed, L"", 0));
    DeleteFileW(doomed.c_str());

    ActionIcons::ShortcutInfo info;
    const auto plainBytes = ReadAll(plain);
    CHECK(!plainBytes.empty());
    CHECK(ActionIcons::ReadShortcut(plainBytes.data(), plainBytes.size(), info));
    CHECK(SameText(info.targetPath, explorer));
    CHECK(info.iconPath.empty());

    const auto iconBytes = ReadAll(withIcon);
    CHECK(ActionIcons::ReadShortcut(iconBytes.data(), iconBytes.size(), info));
    CHECK(SameText(info.targetPath, explorer));
    CHECK(info.iconIndex == 21);
    CHECK(info.iconPath.size() > 0 && info.iconPath.find(L"shell32.dll") != std::wstring::npos);

    // And what a row shows for them: the picture of the program, of the icon file, of a folder -
    // and none for a link whose target is gone.
    CHECK(ActionIcons::ImageForTarget(plain) != nullptr);
    const auto chosen = ActionIcons::ImageForTarget(withIcon);
    const auto ownIcon = ActionIcons::ImageForTarget(plain);
    CHECK(chosen != nullptr);
    // Same picture size, different picture: the icon the link names is the one drawn.
    if (chosen && ownIcon) CHECK(chosen->bgra != ownIcon->bgra);
    CHECK(ActionIcons::ImageForTarget(toFolder) != nullptr);
    CHECK(ActionIcons::ImageForTarget(toNothing) == nullptr);

    for (const std::wstring& f : {plain, withIcon, toFolder, toNothing}) DeleteFileW(f.c_str());
}

void TestAMalformedShortcutIsRefusedNotTrusted() {
    TEST("ActionIcons: a shortcut that is cut short or lying about its sizes is read safely");
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring file = std::wstring(temp) + L"edgedeck-lnk-fuzz.lnk";
    CHECK(MakeShortcut(file, SystemRoot() + L"\\explorer.exe", SystemRoot() + L"\\System32\\shell32.dll", 4));
    const auto good = ReadAll(file);
    DeleteFileW(file.c_str());
    CHECK(good.size() > 100);
    if (good.size() <= 100) return;

    ActionIcons::ShortcutInfo info;
    CHECK(!ActionIcons::ReadShortcut(nullptr, 0, info));
    const std::uint8_t text[] = "this is not a shortcut at all, just some words in a file......................";
    CHECK(!ActionIcons::ReadShortcut(text, sizeof(text), info));

    // Every possible truncation: each one is either refused or yields what came before the cut,
    // and none reads past the end (the vector is exactly this long).
    int refused = 0;
    for (size_t length = 0; length < good.size(); ++length) {
        const std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<ptrdiff_t>(length));
        if (!ActionIcons::ReadShortcut(cut.data(), cut.size(), info)) ++refused;
    }
    CHECK(refused >= 60); // nothing shorter than the header fields it reads (the first 60 bytes) is accepted

    // Every byte of the file set to the worst values in turn: sizes and counts that point far
    // beyond the end. Nothing here may crash or hang.
    for (size_t at = 0; at < good.size(); ++at) {
        for (std::uint8_t value : {std::uint8_t{0xFF}, std::uint8_t{0x00}, std::uint8_t{0x7F}}) {
            std::vector<std::uint8_t> bent = good;
            bent[at] = value;
            ActionIcons::ReadShortcut(bent.data(), bent.size(), info);
        }
    }
    CHECK(true);
}

void TestLookupsAreAnsweredOnTheWindowThatAsked() {
    TEST("ActionIcons: LoadAsync posts one batch to the asking window, in the order asked");
    HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    CHECK(window != nullptr);
    if (!window) return;

    constexpr UINT kMessage = WM_APP + 5;
    const std::wstring program = SystemRoot() + L"\\explorer.exe";
    ActionIcons::LoadAsync({program, L"C:\\no\\such\\edgedeck-missing.exe", L"builtin:desktop"},
                           window, kMessage, 4242);

    ActionIcons::IconBatch* batch = nullptr;
    const DWORD deadline = GetTickCount() + 8000;
    MSG msg;
    while (!batch && GetTickCount() < deadline) {
        if (PeekMessageW(&msg, window, kMessage, kMessage, PM_REMOVE)) {
            batch = reinterpret_cast<ActionIcons::IconBatch*>(msg.wParam);
        } else {
            Sleep(10);
        }
    }
    CHECK(batch != nullptr);
    if (batch) {
        CHECK(batch->Kind() == AsyncKind::ActionIcons);
        CHECK_EQ(batch->requestId, std::uint64_t{4242});
        CHECK_EQ(batch->targets.size(), size_t{3});
        CHECK_EQ(batch->images.size(), size_t{3});
        if (batch->images.size() == 3) {
            CHECK(batch->targets[0] == program);
            CHECK(batch->images[0] != nullptr);
            CHECK(batch->images[1] == nullptr);
            CHECK(batch->images[2] == nullptr);
        }
        delete batch;
    }
    DestroyWindow(window);
}

void TestAnAnswerForAWindowThatIsGoneIsFreed() {
    TEST("ActionIcons: an answer for a window that no longer exists is dropped, not delivered or leaked");
    HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    CHECK(window != nullptr);
    if (!window) return;
    DestroyWindow(window);

    // Nothing to assert on but the absence of a crash or a hang: the batch is deleted by
    // PostOrDelete on the worker. Giving the worker time to finish is the test.
    ActionIcons::LoadAsync({SystemRoot() + L"\\explorer.exe"}, window, WM_APP + 6, 1);
    Sleep(600);
    CHECK(true);
}

} // namespace

void RunActionIconTests() {
    TestBackgroundsSeparateIntoPremultipliedPixels();
    TestAnIconWithAnAlphaChannelIsReadBack();
    TestAnOldMaskedIconIsReadBack();
    TestUnusableIconsGiveNothing();
    // Making shortcuts to read back needs COM; reading them does not (that is the point).
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    TestPicturesComeOnlyFromThingsThatCarryOne();
    TestShortcutsAreReadByHand();
    TestAMalformedShortcutIsRefusedNotTrusted();
    if (SUCCEEDED(apartment)) CoUninitialize();
    TestLookupsAreAnsweredOnTheWindowThatAsked();
    TestAnAnswerForAWindowThatIsGoneIsFreed();
}

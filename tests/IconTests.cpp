// The icon the executable carries, read back out of the built EdgeDeck.exe.
//
// This is a test of the build, not of logic, and it exists for one failure: the resource script
// named the icon with an identifier nothing defined, the resource compiler took it as a *string*
// name, and the code - which asks for a number - never found it. Everything compiled, and the
// notification area showed the generic application icon for as long as the app existed.
#include "TestHarness.h"

#include "Resource.h"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace {

// RT_GROUP_ICON is a wide string pointer in this (UNICODE) build; the narrow API wants this.
const LPCSTR kGroupIconType = MAKEINTRESOURCEA(14);

#ifndef EDGEDECK_EXE_PATH
#error "tests/CMakeLists.txt passes the built executable's path as EDGEDECK_EXE_PATH"
#endif

#pragma pack(push, 2)
struct GroupIconDir {
    WORD reserved;
    WORD type;
    WORD count;
};
struct GroupIconEntry {
    BYTE width;
    BYTE height;
    BYTE colors;
    BYTE reserved;
    WORD planes;
    WORD bitCount;
    DWORD bytesInRes;
    WORD id;
};
#pragma pack(pop)

struct Names {
    std::vector<WORD> numeric;
    int named = 0;
};

BOOL CALLBACK CollectNames(HMODULE, LPCSTR, LPSTR name, LONG_PTR param) {
    auto* names = reinterpret_cast<Names*>(param);
    if (IS_INTRESOURCE(name)) {
        names->numeric.push_back(static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name)));
    } else {
        ++names->named;
    }
    return TRUE;
}

void TestTheExecutableCarriesItsIconUnderTheNumberTheCodeAsksFor() {
    TEST("Icon: EdgeDeck.exe holds its icon under id IDI_EDGEDECK, and under no text name");

    HMODULE module = LoadLibraryExA(EDGEDECK_EXE_PATH, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    CHECK(module != nullptr);
    if (!module) return;

    // What the code loads: this is the call that used to come back empty.
    HRSRC group = FindResourceA(module, MAKEINTRESOURCEA(IDI_EDGEDECK), kGroupIconType);
    CHECK(group != nullptr);

    // Explorer shows the first icon group for the file, and a string name sorts after every
    // number: a stray named group would not hide this one, but it is the sign of the same slip.
    Names names;
    EnumResourceNamesA(module, kGroupIconType, CollectNames, reinterpret_cast<LONG_PTR>(&names));
    CHECK_EQ(names.named, 0);
    CHECK(!names.numeric.empty());
    if (!names.numeric.empty()) {
        CHECK_EQ(static_cast<int>(*std::min_element(names.numeric.begin(), names.numeric.end())),
                 static_cast<int>(IDI_EDGEDECK));
    }

    if (group) {
        HGLOBAL loaded = LoadResource(module, group);
        const auto* dir = loaded ? static_cast<const GroupIconDir*>(LockResource(loaded)) : nullptr;
        CHECK(dir != nullptr);
        if (dir) {
            const auto* entries = reinterpret_cast<const GroupIconEntry*>(dir + 1);
            auto has = [&](int size) {
                for (int i = 0; i < dir->count; ++i) {
                    const int w = entries[i].width == 0 ? 256 : entries[i].width;
                    if (w == size) return true;
                }
                return false;
            };
            // The sizes the shell asks for: 16/20/24/32 in the notification area and title bars
            // from 100 % to 200 % scaling, 48 and up in Explorer and the taskbar.
            for (int size : {16, 20, 24, 32, 48, 256}) {
                CHECK(has(size));
            }
        }
    }
    FreeLibrary(module);
}

} // namespace

void RunIconTests() { TestTheExecutableCarriesItsIconUnderTheNumberTheCodeAsksFor(); }

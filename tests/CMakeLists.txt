# The tests link only the translation units that hold pure logic.
# LrcLyrics.cpp is not among them in spirit: the parser lives in the same file
# as the WinRT/WinHTTP fetch, so the test target compiles that file whole. That
# coupling is called out in the file's own comments.
add_executable(EdgeDeckTests
    Main.cpp
    TestHarness.h
    TestHarness.cpp
    LyricsTests.cpp
    ConfigTests.cpp
    DisplayTests.cpp
    LayeredTests.cpp
    NoticeTests.cpp
    AutostartTests.cpp
    CrashTests.cpp
    FullscreenTests.cpp
    PlacementTests.cpp
    IconTests.cpp
    QuickActionTests.cpp
    ActionIconTests.cpp
    RecentFilesTests.cpp
    StringsTests.cpp
    ShortcutsTests.cpp
    DisplayLayoutsTests.cpp
    BugReportTests.cpp
    ${CMAKE_SOURCE_DIR}/src/ActionIcons.cpp
    ${CMAKE_SOURCE_DIR}/src/BugReport.cpp
    ${CMAKE_SOURCE_DIR}/src/RecentFiles.cpp
    ${CMAKE_SOURCE_DIR}/src/Strings.cpp
    ${CMAKE_SOURCE_DIR}/src/Shortcuts.cpp
    ${CMAKE_SOURCE_DIR}/src/DisplayLayouts.cpp
    ${CMAKE_SOURCE_DIR}/src/Fullscreen.cpp
    ${CMAKE_SOURCE_DIR}/src/Placement.cpp
    ${CMAKE_SOURCE_DIR}/src/QuickAction.cpp
    ${CMAKE_SOURCE_DIR}/src/Actions.cpp
    ${CMAKE_SOURCE_DIR}/src/Autostart.cpp
    ${CMAKE_SOURCE_DIR}/src/CrashHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/DisplayTopology.cpp
    ${CMAKE_SOURCE_DIR}/src/LayeredTarget.cpp
    ${CMAKE_SOURCE_DIR}/src/Diagnostics.cpp
    ${CMAKE_SOURCE_DIR}/src/LrcLyrics.cpp
    ${CMAKE_SOURCE_DIR}/src/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/AsyncResult.h
    ${CMAKE_SOURCE_DIR}/src/PanelWidget.h
)

target_compile_definitions(EdgeDeckTests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

target_include_directories(EdgeDeckTests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/res)

# IconTests reads the icon back out of the built executable, so that one has to exist first, and
# the test has to know where it is.
add_dependencies(EdgeDeckTests EdgeDeck)
target_compile_definitions(EdgeDeckTests PRIVATE EDGEDECK_EXE_PATH="$<TARGET_FILE:EdgeDeck>")

target_link_libraries(EdgeDeckTests PRIVATE
    d2d1
    advapi32
    dwmapi
    user32
    gdi32
    shell32
    ole32
    oleaut32
    windowsapp
    winhttp
    shlwapi
    shcore
)

if (MSVC)
    target_compile_options(EdgeDeckTests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(EdgeDeckTests PRIVATE -Wall -Wextra)
endif()

add_test(NAME unit COMMAND EdgeDeckTests)

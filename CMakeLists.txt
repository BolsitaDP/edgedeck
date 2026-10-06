cmake_minimum_required(VERSION 3.20)
project(EdgeDeck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The C and C++ runtime is linked into the executable (/MT) instead of loaded from
# MSVCP140.dll / VCRUNTIME140*.dll. Those come from the "Visual C++ Redistributable", which
# a clean Windows install does not have: a copy of EdgeDeck.exe on such a machine would
# fail to start with a missing-DLL dialog. Set before any target so that the tests and the
# tools are built the same way as the product.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Pure-logic code (the LRC parser, the config reader/writer) is compiled a second
# time into a small console-free test binary, so CI can cover the parts that
# have no windowing in them.
option(EDGEDECK_BUILD_TESTS "Build the EdgeDeck unit tests" ON)

# Developer test tools (tools/README.md): a display-switching CLI, harnesses that host the
# Settings window or the whole App, and plain windows to test against. They need a desktop
# session and real monitors, so nothing runs them in CI; CI only builds them, which is what
# keeps them in step with the code they call.
option(EDGEDECK_BUILD_TOOLS "Build the developer test tools" OFF)

add_executable(EdgeDeck WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/Tab.cpp
    src/Tab.h
    src/AsyncResult.h
    src/PanelWidget.h
    src/QuickActionsWidget.cpp
    src/QuickActionsWidget.h
    src/MediaWidget.cpp
    src/MediaWidget.h
    src/BrightnessWidget.cpp
    src/BrightnessWidget.h
    src/LyricsWidget.cpp
    src/LyricsWidget.h
    src/LrcLyrics.cpp
    src/LrcLyrics.h
    src/VolumeWidget.cpp
    src/VolumeWidget.h
    src/VolumeControls.cpp
    src/VolumeControls.h
    src/DisplayWidget.cpp
    src/DisplayWidget.h
    src/DisplayControls.cpp
    src/DisplayControls.h
    src/DisplayTopology.cpp
    src/DisplayTopology.h
    src/BrightnessControls.cpp
    src/BrightnessControls.h
    src/Fullscreen.cpp
    src/Fullscreen.h
    src/Placement.cpp
    src/Placement.h
    src/Autostart.cpp
    src/Autostart.h
    src/CrashHandler.cpp
    src/CrashHandler.h
    src/SettingsWindow.cpp
    src/SettingsWindow.h
    src/Config.cpp
    src/Config.h
    src/Diagnostics.cpp
    src/Diagnostics.h
    src/Renderer.cpp
    src/LayeredTarget.cpp
    src/LayeredTarget.h
    src/Renderer.h
    src/Actions.cpp
    src/Actions.h
    src/MediaControls.cpp
    src/MediaControls.h
    res/EdgeDeck.rc
)

target_compile_definitions(EdgeDeck PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

target_link_libraries(EdgeDeck PRIVATE
    d2d1
    dwrite
    user32
    gdi32
    shell32
    ole32
    oleaut32
    shcore
    windowsapp
    comctl32
    uxtheme
    dwmapi
    dxva2
    advapi32
    winhttp
    mmdevapi
    propsys
    uuid
    avrt
)
if (MSVC)
    # /utf-8 pins the source and execution character sets, so a stray non-ASCII
    # character in a string literal or a comment is a non-issue rather than a
    # build that depends on the code page it happens to be compiled under.
    target_compile_options(EdgeDeck PRIVATE /W4 /permissive- /utf-8)

    # A .pdb next to the exe, so that "EdgeDeck.exe+0x1A2B3C" from a crash line and the minidump
    # beside it can be turned into a function and a line. Symbols live in the separate file;
    # /OPT:REF and /OPT:ICF restore the dead-code removal that /DEBUG would otherwise switch off,
    # so the executable itself is no bigger.
    target_compile_options(EdgeDeck PRIVATE /Zi)
    target_link_options(EdgeDeck PRIVATE /DEBUG /OPT:REF /OPT:ICF)

    # The manifest (DPI awareness, UTF-8 code page, supported OS, and the
    # Common-Controls v6 dependency) is merged into the executable by the
    # resource script, which is the only way to get DPI awareness applied before
    # the first window is created.
    target_sources(EdgeDeck PRIVATE res/EdgeDeck.manifest)
else()
    target_compile_options(EdgeDeck PRIVATE -Wall -Wextra)
endif()

if (EDGEDECK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# After the EdgeDeck target exists: the tools read its source list instead of repeating it.
if (EDGEDECK_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

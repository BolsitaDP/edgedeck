cmake_minimum_required(VERSION 3.20)
project(EdgeDeck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(EdgeDeck WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/Tab.cpp
    src/Tab.h
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
    src/BrightnessControls.cpp
    src/BrightnessControls.h
    src/Autostart.cpp
    src/Autostart.h
    src/SettingsWindow.cpp
    src/SettingsWindow.h
    src/Config.cpp
    src/Config.h
    src/Renderer.cpp
    src/Renderer.h
    src/Actions.cpp
    src/Actions.h
    src/MediaControls.cpp
    src/MediaControls.h
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
    dxva2
    advapi32
    winhttp
)

if (MSVC)
    target_compile_options(EdgeDeck PRIVATE /W4 /permissive-)
    # Merges the comctl32 v6 dependency into MSVC's own auto-generated
    # embedded manifest (rather than embedding a second manifest resource
    # ourselves, which collides with the linker's default one) - needed for
    # the settings window's common controls to render themed.
    target_link_options(EdgeDeck PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )
else()
    target_compile_options(EdgeDeck PRIVATE -Wall -Wextra)
endif()

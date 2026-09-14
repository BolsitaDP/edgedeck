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
    src/EdgeWindow.cpp
    src/EdgeWindow.h
    src/Renderer.cpp
    src/Renderer.h
    src/Actions.cpp
    src/Actions.h
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
)

if (MSVC)
    target_compile_options(EdgeDeck PRIVATE /W4 /permissive-)
else()
    target_compile_options(EdgeDeck PRIVATE -Wall -Wextra)
endif()

# Developer test tools. Not part of the product and not built by default (see
# EDGEDECK_BUILD_TOOLS in the root CMakeLists.txt); CI builds them so they cannot rot,
# but nothing runs them there - they need a desktop session, a mouse and real monitors.
#
# What they are for and how to run them is in tools/README.md.

# The tools that host real EdgeDeck code (a settings window, the whole App) are built from
# the same sources as the product, so there is no second list to keep in step: whatever
# EdgeDeck is made of, minus the entry point and the resource script, is what they get.
get_target_property(core_sources EdgeDeck SOURCES)
list(FILTER core_sources EXCLUDE REGEX "(^|/)main\\.cpp$")
list(FILTER core_sources EXCLUDE REGEX "\\.(rc|manifest)$")
list(TRANSFORM core_sources PREPEND "${CMAKE_SOURCE_DIR}/")

get_target_property(core_libs EdgeDeck LINK_LIBRARIES)
get_target_property(core_defs EdgeDeck COMPILE_DEFINITIONS)

# name, then its sources. DPI awareness and the common-controls manifest come from the
# product's own manifest, so a tool behaves like the app does on a scaled monitor.
function(edgedeck_tool name)
    add_executable(${name} ${ARGN} ${CMAKE_SOURCE_DIR}/res/EdgeDeck.manifest)
    target_compile_definitions(${name} PRIVATE ${core_defs})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if (MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive- /utf-8)
    endif()
endfunction()

# Which monitors are on, and switching them, using the same code as the Displays tab.
edgedeck_tool(dispcfg
    src/dispcfg.cpp
    ${CMAKE_SOURCE_DIR}/src/DisplayTopology.cpp)
target_link_libraries(dispcfg PRIVATE user32)

# Just the Settings window, with a layout report (overlaps, clipped text) at any DPI.
edgedeck_tool(settingsharness src/settingsharness.cpp ${core_sources})
target_link_libraries(settingsharness PRIVATE ${core_libs})

# The whole App inside this process, with problem reports injected from outside.
edgedeck_tool(apptest src/apptest.cpp ${core_sources})
target_link_libraries(apptest PRIVATE ${core_libs})

# Plain windows to stand behind or in front of EdgeDeck's own.
edgedeck_tool(backdrop src/backdrop.cpp)
target_link_libraries(backdrop PRIVATE user32 gdi32)
edgedeck_tool(fullscreenwin src/fullscreenwin.cpp)
target_link_libraries(fullscreenwin PRIVATE user32 gdi32)

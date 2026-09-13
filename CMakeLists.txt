cmake_minimum_required(VERSION 3.16)
project(touch_ui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT TARGET SDL3::SDL3 AND NOT TARGET SDL3::SDL3-static)
    find_package(SDL3 CONFIG REQUIRED)
endif()

# Lucent owns the contact-to-zone router this composes; SDL3_image rasterises
# the shared SVG art at the size actually drawn.
set(TOUCH_UI_LUCENT_DEFAULT "")
foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../lucent" "${CMAKE_CURRENT_SOURCE_DIR}/../../lucent")
    if(IS_DIRECTORY "${candidate}")
        set(TOUCH_UI_LUCENT_DEFAULT "${candidate}")
        break()
    endif()
endforeach()
if(DEFINED ENV{TOUCH_UI_LUCENT_DIR} AND NOT "$ENV{TOUCH_UI_LUCENT_DIR}" STREQUAL "")
    set(TOUCH_UI_LUCENT_DEFAULT "$ENV{TOUCH_UI_LUCENT_DIR}")
elseif(DEFINED ENV{SHARED_LUCENT_DIR} AND NOT "$ENV{SHARED_LUCENT_DIR}" STREQUAL "")
    set(TOUCH_UI_LUCENT_DEFAULT "$ENV{SHARED_LUCENT_DIR}")
endif()
set(TOUCH_UI_LUCENT_DIR "${TOUCH_UI_LUCENT_DEFAULT}" CACHE PATH "Path to the lucent checkout")
if(NOT IS_DIRECTORY "${TOUCH_UI_LUCENT_DIR}" OR NOT TARGET lucent::lucent)
    if(IS_DIRECTORY "${TOUCH_UI_LUCENT_DIR}")
        set(LUCENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${TOUCH_UI_LUCENT_DIR}" "${CMAKE_BINARY_DIR}/lucent")
    else()
        message(FATAL_ERROR
            "touch_ui requires a lucent checkout at ${TOUCH_UI_LUCENT_DIR} or an "
            "already-added lucent::lucent target")
    endif()
endif()

# The art is consumed from shared/port-assets, never copied into a project.
set(TOUCH_UI_PORT_ASSETS_DEFAULT "")
foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../port-assets"
                  "${CMAKE_CURRENT_SOURCE_DIR}/../../port-assets")
    if(IS_DIRECTORY "${candidate}")
        set(TOUCH_UI_PORT_ASSETS_DEFAULT "${candidate}")
        break()
    endif()
endforeach()
if(DEFINED ENV{PORT_ASSETS_DIR} AND NOT "$ENV{PORT_ASSETS_DIR}" STREQUAL "")
    set(TOUCH_UI_PORT_ASSETS_DEFAULT "$ENV{PORT_ASSETS_DIR}")
endif()
set(TOUCH_UI_PORT_ASSETS_DIR "${TOUCH_UI_PORT_ASSETS_DEFAULT}"
    CACHE PATH "Path to the shared port-assets checkout")
if(NOT IS_DIRECTORY "${TOUCH_UI_PORT_ASSETS_DIR}/sets/touch-controls")
    message(FATAL_ERROR
        "touch_ui requires the shared port-assets checkout at "
        "${TOUCH_UI_PORT_ASSETS_DIR}; provide -DTOUCH_UI_PORT_ASSETS_DIR=<checkout>")
endif()

# SDL3_image rasterises the shared SVG art; it is a private implementation
# detail of the presentation, not part of this library's interface.
if(NOT TARGET SDL3_image::SDL3_image)
    find_package(SDL3_image CONFIG REQUIRED)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TOUCH_UI_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${TOUCH_UI_GENERATED_DIR}")
add_custom_command(
    OUTPUT "${TOUCH_UI_GENERATED_DIR}/embedded_icons.cpp"
           "${TOUCH_UI_GENERATED_DIR}/embedded_icons.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_svg.py"
            --assets "${TOUCH_UI_PORT_ASSETS_DIR}"
            --source "${TOUCH_UI_GENERATED_DIR}/embedded_icons.cpp"
            --header "${TOUCH_UI_GENERATED_DIR}/embedded_icons.h"
    DEPENDS "${TOUCH_UI_PORT_ASSETS_DIR}/sets/touch-controls/set.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_svg.py"
    VERBATIM)

add_library(touch_ui STATIC
    src/controls.cpp
    src/layout.cpp
    "${TOUCH_UI_GENERATED_DIR}/embedded_icons.cpp")
add_library(touch_ui::touch_ui ALIAS touch_ui)
target_include_directories(touch_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${TOUCH_UI_GENERATED_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(touch_ui PUBLIC lucent::lucent)
if(TARGET SDL3::SDL3)
    target_link_libraries(touch_ui PUBLIC SDL3::SDL3)
else()
    target_link_libraries(touch_ui PUBLIC SDL3::SDL3-static)
endif()
target_link_libraries(touch_ui PRIVATE SDL3_image::SDL3_image)
target_compile_options(touch_ui PRIVATE -Wall -Wextra -Wpedantic)

# The module's own tests and capture tool run where they can be executed: a
# cross-compiled consumer build (Android) links the library only.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_CROSSCOMPILING)
    # A capture binary keeps the drawn result visible on a host that cannot run
    # the port itself; tools/capture.py drives it.
    add_executable(touch_ui_capture tests/capture_touch_ui.cpp)
    target_link_libraries(touch_ui_capture PRIVATE touch_ui)
    target_include_directories(touch_ui_capture PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include" "${TOUCH_UI_GENERATED_DIR}")

    enable_testing()
    add_executable(touch_ui_tests tests/test_touch_ui.cpp)
    target_link_libraries(touch_ui_tests PRIVATE touch_ui)
    target_include_directories(touch_ui_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include" "${TOUCH_UI_GENERATED_DIR}")
    add_test(NAME touch_ui_tests COMMAND touch_ui_tests)
endif()

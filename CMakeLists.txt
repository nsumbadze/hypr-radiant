cmake_minimum_required(VERSION 3.25)

project(
    hypr-radiant
    VERSION 0.1.0
    DESCRIPTION "Minimal Hyprland plugin skeleton for hypr-radiant"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

add_library(hypr-radiant MODULE
    src/main.cpp
)

target_include_directories(hypr-radiant
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(hypr-radiant
    PRIVATE
        -fno-gnu-unique
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(hypr-radiant
    PRIVATE
        PkgConfig::HYPRLAND
)

set_target_properties(hypr-radiant PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    PREFIX ""
    OUTPUT_NAME "hypr-radiant"
    SUFFIX ".so"
)

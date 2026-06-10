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
    src/Config.cpp
    src/FadeAnimation.cpp
    src/HitTester.cpp
    src/main.cpp
    src/OverlayRenderer.cpp
    src/WorkspaceWallLayout.cpp
    src/StateCollector.cpp
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

include(CTest)

if(BUILD_TESTING)
    add_executable(hypr-radiant-layout-tests
        tests/WorkspaceWallLayoutTest.cpp
        src/WorkspaceWallLayout.cpp
    )

    target_include_directories(hypr-radiant-layout-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(hypr-radiant-layout-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-layout-tests PRIVATE -UNDEBUG)

    add_test(NAME hypr-radiant-layout-tests COMMAND hypr-radiant-layout-tests)

    add_executable(hypr-radiant-hit-tests
        tests/HitTesterTest.cpp
        src/WorkspaceWallLayout.cpp
        src/HitTester.cpp
    )

    target_include_directories(hypr-radiant-hit-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(hypr-radiant-hit-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-hit-tests PRIVATE -UNDEBUG)

    add_test(NAME hypr-radiant-hit-tests COMMAND hypr-radiant-hit-tests)
endif()

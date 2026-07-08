cmake_minimum_required(VERSION 3.25)

project(
    hypr-radiant
    VERSION 0.1.0
    DESCRIPTION "Minimal Hyprland plugin skeleton for hypr-radiant"
    LANGUAGES CXX
)

option(HYPR_RADIANT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer for tests" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)

function(hypr_radiant_enable_warnings target)
    target_compile_options(${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endfunction()

set(HYPR_RADIANT_SANITIZER_FLAGS
    -fsanitize=address,undefined
)

function(hypr_radiant_enable_sanitizers target)
    if(HYPR_RADIANT_ENABLE_SANITIZERS)
        target_compile_options(${target}
            PRIVATE
                ${HYPR_RADIANT_SANITIZER_FLAGS}
        )
        target_link_options(${target}
            PRIVATE
                ${HYPR_RADIANT_SANITIZER_FLAGS}
        )
    endif()
endfunction()

add_library(hypr-radiant MODULE
    src/ActivationController.cpp
    src/Config.cpp
    src/FadeAnimation.cpp
    src/HitTester.cpp
    src/InputController.cpp
    src/main.cpp
    src/OverlayRenderer.cpp
    src/SearchMatcher.cpp
    src/SearchPanelGeometry.cpp
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
)

hypr_radiant_enable_warnings(hypr-radiant)

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
        src/SearchPanelGeometry.cpp
    )

    target_include_directories(hypr-radiant-layout-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(hypr-radiant-layout-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-layout-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-layout-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-layout-tests)

    add_test(NAME hypr-radiant-layout-tests COMMAND hypr-radiant-layout-tests)

    add_executable(hypr-radiant-hit-tests
        tests/HitTesterTest.cpp
        src/HitTester.cpp
    )

    target_include_directories(hypr-radiant-hit-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(hypr-radiant-hit-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-hit-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-hit-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-hit-tests)

    add_test(NAME hypr-radiant-hit-tests COMMAND hypr-radiant-hit-tests)

    add_executable(hypr-radiant-search-tests
        tests/SearchMatcherTest.cpp
        src/SearchMatcher.cpp
    )

    target_include_directories(hypr-radiant-search-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(hypr-radiant-search-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-search-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-search-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-search-tests)

    add_test(NAME hypr-radiant-search-tests COMMAND hypr-radiant-search-tests)

    add_executable(hypr-radiant-config-tests
        tests/ConfigParserTest.cpp
        src/Config.cpp
    )

    target_include_directories(hypr-radiant-config-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(hypr-radiant-config-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-config-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-config-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-config-tests)
    target_link_libraries(hypr-radiant-config-tests
        PRIVATE
            PkgConfig::HYPRLAND
    )

    add_test(NAME hypr-radiant-config-tests COMMAND hypr-radiant-config-tests)

    add_executable(hypr-radiant-fade-tests
        tests/FadeAnimationTest.cpp
        src/FadeAnimation.cpp
    )

    target_include_directories(hypr-radiant-fade-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_features(hypr-radiant-fade-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-fade-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-fade-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-fade-tests)

    add_test(NAME hypr-radiant-fade-tests COMMAND hypr-radiant-fade-tests)
endif()

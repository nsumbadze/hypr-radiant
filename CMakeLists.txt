cmake_minimum_required(VERSION 3.25)

project(
    hypr-radiant
    VERSION 0.3.0
    DESCRIPTION "Native workspace overview for Hyprland"
    LANGUAGES CXX
)

option(HYPR_RADIANT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer for tests" OFF)
option(HYPR_RADIANT_BUILD_PLUGIN "Build the Hyprland plugin" ON)
option(HYPR_RADIANT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(HYPR_RADIANT_BUILD_PLUGIN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
endif()

function(hypr_radiant_enable_warnings target)
    target_compile_options(${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
    if(HYPR_RADIANT_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
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

if(HYPR_RADIANT_BUILD_PLUGIN)
    add_library(hypr-radiant MODULE
        src/ActivationController.cpp
        src/AppIdentity.cpp
        src/Color.cpp
        src/Config.cpp
        src/FadeAnimation.cpp
        src/OmarchyPalette.cpp
        src/GestureController.cpp
        src/SwipeTracker.cpp
        src/HitTester.cpp
        src/InputController.cpp
        src/main.cpp
        src/OverlayRenderer.cpp
        src/OpeningInputGuard.cpp
        src/SearchMatcher.cpp
        src/SearchPanelGeometry.cpp
        src/StageTransform.cpp
        src/OverlayGeometry.cpp
        src/WorkspaceWallLayout.cpp
        src/StateCollector.cpp
        src/Theme.cpp
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
endif()

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

    if(HYPR_RADIANT_BUILD_PLUGIN)
        add_executable(hypr-radiant-config-tests
            tests/ConfigParserTest.cpp
            tests/stubs/HyprlandConfigStub.cpp
            src/Color.cpp
            src/Config.cpp
            src/OmarchyPalette.cpp
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
    endif()

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

    add_executable(hypr-radiant-gesture-tests
        tests/GestureControllerTest.cpp
        src/SwipeTracker.cpp
    )
    target_include_directories(hypr-radiant-gesture-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-gesture-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-gesture-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-gesture-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-gesture-tests)
    add_test(NAME hypr-radiant-gesture-tests COMMAND hypr-radiant-gesture-tests)

    add_executable(hypr-radiant-input-guard-tests
        tests/OpeningInputGuardTest.cpp
        src/OpeningInputGuard.cpp
    )
    target_include_directories(hypr-radiant-input-guard-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-input-guard-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-input-guard-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-input-guard-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-input-guard-tests)
    add_test(NAME hypr-radiant-input-guard-tests COMMAND hypr-radiant-input-guard-tests)

    add_executable(hypr-radiant-omarchy-palette-tests
        tests/OmarchyPaletteTest.cpp
        src/Color.cpp
        src/OmarchyPalette.cpp
    )
    target_include_directories(hypr-radiant-omarchy-palette-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-omarchy-palette-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-omarchy-palette-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-omarchy-palette-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-omarchy-palette-tests)
    add_test(NAME hypr-radiant-omarchy-palette-tests COMMAND hypr-radiant-omarchy-palette-tests)

    add_executable(hypr-radiant-app-identity-tests
        tests/AppIdentityTest.cpp
        src/AppIdentity.cpp
    )
    target_include_directories(hypr-radiant-app-identity-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-app-identity-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-app-identity-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-app-identity-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-app-identity-tests)
    add_test(NAME hypr-radiant-app-identity-tests COMMAND hypr-radiant-app-identity-tests)

    add_executable(hypr-radiant-stage-transform-tests
        tests/StageTransformTest.cpp
        src/StageTransform.cpp
    )
    target_include_directories(hypr-radiant-stage-transform-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-stage-transform-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-stage-transform-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-stage-transform-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-stage-transform-tests)
    add_test(NAME hypr-radiant-stage-transform-tests COMMAND hypr-radiant-stage-transform-tests)

    add_executable(hypr-radiant-overlay-geometry-tests
        tests/OverlayGeometryTest.cpp
        src/OverlayGeometry.cpp
        src/StageTransform.cpp
    )
    target_include_directories(hypr-radiant-overlay-geometry-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-overlay-geometry-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-overlay-geometry-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-overlay-geometry-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-overlay-geometry-tests)
    add_test(NAME hypr-radiant-overlay-geometry-tests COMMAND hypr-radiant-overlay-geometry-tests)
endif()

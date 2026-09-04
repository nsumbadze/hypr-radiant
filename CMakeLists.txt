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
    pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET "hyprland>=0.55")
    pkg_check_modules(AQUAMARINE REQUIRED aquamarine)
    pkg_check_modules(HYPRUTILS REQUIRED hyprutils)
    pkg_check_modules(HYPRGRAPHICS REQUIRED hyprgraphics)
    pkg_check_modules(HYPRLANG REQUIRED hyprlang)

    message(STATUS
        "Hyprland stack: Hyprland ${HYPRLAND_VERSION}, Aquamarine ${AQUAMARINE_VERSION}, "
        "hyprutils ${HYPRUTILS_VERSION}, hyprgraphics ${HYPRGRAPHICS_VERSION}, hyprlang ${HYPRLANG_VERSION}"
    )

    if(HYPRLAND_VERSION VERSION_GREATER_EQUAL "0.56")
        set(HYPR_RADIANT_HYPRLAND_STATE_API 1)
        set(HYPR_RADIANT_HYPRLAND_LOGGER_NEEDS_ENV_STUB 0)
    else()
        set(HYPR_RADIANT_HYPRLAND_STATE_API 0)
        set(HYPR_RADIANT_HYPRLAND_LOGGER_NEEDS_ENV_STUB 1)
    endif()

    if(HYPRLAND_VERSION VERSION_GREATER_EQUAL "0.55.4")
        set(HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE 1)
    else()
        set(HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE 0)
    endif()
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
        src/compositor/ActivationController.cpp
        src/overview/AppIdentity.cpp
        src/config/Color.cpp
        src/config/Config.cpp
        src/config/HyprlandDecoration.cpp
        src/render/FadeAnimation.cpp
        src/render/ChromeStyle.cpp
        src/config/OmarchyPalette.cpp
        src/config/Preferences.cpp
        src/input/GestureController.cpp
        src/input/KeyboardAction.cpp
        src/input/SwipeTracker.cpp
        src/overview/HitTester.cpp
        src/input/InputController.cpp
        src/main.cpp
        src/render/OverlayRenderer.cpp
        src/input/OpeningInputGuard.cpp
        src/input/ShortcutController.cpp
        src/overview/SearchMatcher.cpp
        src/overview/SearchSuggestions.cpp
        src/overview/SearchPanelGeometry.cpp
        src/overview/StageTransform.cpp
        src/overview/OverlayGeometry.cpp
        src/overview/PreferencesPanelGeometry.cpp
        src/render/LabelRenderer.cpp
        src/overview/WorkspaceWallLayout.cpp
        src/compositor/StateCollector.cpp
        src/render/Theme.cpp
    )

    target_include_directories(hypr-radiant
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_options(hypr-radiant
        PRIVATE
            -fno-gnu-unique
    )

    target_compile_definitions(hypr-radiant
        PRIVATE
            HYPR_RADIANT_HYPRLAND_STATE_API=${HYPR_RADIANT_HYPRLAND_STATE_API}
            HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE=${HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE}
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
        src/overview/WorkspaceWallLayout.cpp
        src/overview/SearchPanelGeometry.cpp
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

    add_executable(hypr-radiant-chrome-style-tests
        tests/ChromeStyleTest.cpp
        src/render/ChromeStyle.cpp
    )
    target_include_directories(hypr-radiant-chrome-style-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-chrome-style-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-chrome-style-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-chrome-style-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-chrome-style-tests)
    add_test(NAME hypr-radiant-chrome-style-tests COMMAND hypr-radiant-chrome-style-tests)

    add_executable(hypr-radiant-hit-tests
        tests/HitTesterTest.cpp
        src/overview/HitTester.cpp
        src/overview/OverlayGeometry.cpp
        src/overview/StageTransform.cpp
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
        src/overview/SearchMatcher.cpp
        src/overview/SearchSuggestions.cpp
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
            src/config/Color.cpp
            src/config/Config.cpp
            src/config/OmarchyPalette.cpp
            src/render/ChromeStyle.cpp
        )

        target_include_directories(hypr-radiant-config-tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_include_directories(hypr-radiant-config-tests
            SYSTEM PRIVATE
                ${HYPRLAND_INCLUDE_DIRS}
        )

        target_compile_features(hypr-radiant-config-tests PRIVATE cxx_std_23)
        target_compile_options(hypr-radiant-config-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
        target_compile_definitions(hypr-radiant-config-tests
            PRIVATE
                HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE=${HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE}
                HYPR_RADIANT_HYPRLAND_LOGGER_NEEDS_ENV_STUB=${HYPR_RADIANT_HYPRLAND_LOGGER_NEEDS_ENV_STUB}
        )
        hypr_radiant_enable_warnings(hypr-radiant-config-tests)
        hypr_radiant_enable_sanitizers(hypr-radiant-config-tests)
        target_link_directories(hypr-radiant-config-tests PRIVATE ${HYPRUTILS_LIBRARY_DIRS})
        target_link_libraries(hypr-radiant-config-tests PRIVATE ${HYPRUTILS_LIBRARIES})
        add_test(NAME hypr-radiant-config-tests COMMAND hypr-radiant-config-tests)
    endif()

    add_executable(hypr-radiant-fade-tests
        tests/FadeAnimationTest.cpp
        src/render/FadeAnimation.cpp
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
        src/input/SwipeTracker.cpp
    )
    target_include_directories(hypr-radiant-gesture-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-gesture-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-gesture-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-gesture-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-gesture-tests)
    add_test(NAME hypr-radiant-gesture-tests COMMAND hypr-radiant-gesture-tests)

    add_executable(hypr-radiant-input-guard-tests
        tests/OpeningInputGuardTest.cpp
        src/input/OpeningInputGuard.cpp
    )
    target_include_directories(hypr-radiant-input-guard-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-input-guard-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-input-guard-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-input-guard-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-input-guard-tests)
    add_test(NAME hypr-radiant-input-guard-tests COMMAND hypr-radiant-input-guard-tests)

    add_executable(hypr-radiant-keyboard-action-tests
        tests/KeyboardActionTest.cpp
        src/input/KeyboardAction.cpp
    )
    target_include_directories(hypr-radiant-keyboard-action-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-keyboard-action-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-keyboard-action-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-keyboard-action-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-keyboard-action-tests)
    add_test(NAME hypr-radiant-keyboard-action-tests COMMAND hypr-radiant-keyboard-action-tests)

    add_executable(hypr-radiant-omarchy-palette-tests
        tests/OmarchyPaletteTest.cpp
        src/config/Color.cpp
        src/config/OmarchyPalette.cpp
    )
    target_include_directories(hypr-radiant-omarchy-palette-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-omarchy-palette-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-omarchy-palette-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-omarchy-palette-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-omarchy-palette-tests)
    add_test(NAME hypr-radiant-omarchy-palette-tests COMMAND hypr-radiant-omarchy-palette-tests)

    add_executable(hypr-radiant-preferences-tests
        tests/PreferencesTest.cpp
        src/config/Preferences.cpp
    )
    target_include_directories(hypr-radiant-preferences-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-preferences-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-preferences-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-preferences-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-preferences-tests)
    add_test(NAME hypr-radiant-preferences-tests COMMAND hypr-radiant-preferences-tests)

    add_executable(hypr-radiant-app-identity-tests
        tests/AppIdentityTest.cpp
        src/overview/AppIdentity.cpp
    )
    target_include_directories(hypr-radiant-app-identity-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-app-identity-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-app-identity-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-app-identity-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-app-identity-tests)
    add_test(NAME hypr-radiant-app-identity-tests COMMAND hypr-radiant-app-identity-tests)

    add_executable(hypr-radiant-stage-transform-tests
        tests/StageTransformTest.cpp
        src/overview/StageTransform.cpp
    )
    target_include_directories(hypr-radiant-stage-transform-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-stage-transform-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-stage-transform-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-stage-transform-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-stage-transform-tests)
    add_test(NAME hypr-radiant-stage-transform-tests COMMAND hypr-radiant-stage-transform-tests)

    add_executable(hypr-radiant-overlay-geometry-tests
        tests/OverlayGeometryTest.cpp
        src/overview/OverlayGeometry.cpp
        src/overview/StageTransform.cpp
    )
    target_include_directories(hypr-radiant-overlay-geometry-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-overlay-geometry-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-overlay-geometry-tests PRIVATE -UNDEBUG -Wno-missing-field-initializers)
    hypr_radiant_enable_warnings(hypr-radiant-overlay-geometry-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-overlay-geometry-tests)
    add_test(NAME hypr-radiant-overlay-geometry-tests COMMAND hypr-radiant-overlay-geometry-tests)

    add_executable(hypr-radiant-preferences-panel-tests
        tests/PreferencesPanelGeometryTest.cpp
        src/overview/PreferencesPanelGeometry.cpp
    )
    target_include_directories(hypr-radiant-preferences-panel-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(hypr-radiant-preferences-panel-tests PRIVATE cxx_std_23)
    target_compile_options(hypr-radiant-preferences-panel-tests PRIVATE -UNDEBUG)
    hypr_radiant_enable_warnings(hypr-radiant-preferences-panel-tests)
    hypr_radiant_enable_sanitizers(hypr-radiant-preferences-panel-tests)
    add_test(NAME hypr-radiant-preferences-panel-tests COMMAND hypr-radiant-preferences-panel-tests)
endif()

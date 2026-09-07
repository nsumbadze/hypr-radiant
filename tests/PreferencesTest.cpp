#include <hypr-radiant/config/Preferences.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

void defaultsFollowExistingConfig() {
    const auto preferences = parsePreferences("");
    assert(preferences.workspaceView == WorkspaceViewPreference::FollowConfig);
    assert(preferences.windowView == WindowViewPreference::Spatial);
    assert(preferences.motion == MotionPreference::FollowConfig);
    assert(preferences.chrome == ChromePreference::FollowConfig);
    assert(preferences.shelf == ShelfPreference::FollowConfig);
    assert(preferences.windowNavigation == WindowNavigationPreference::FollowConfig);
    assert(preferences.nativeTheme.empty());
}

void parsesEveryPreference() {
    const auto preferences = parsePreferences(R"(
workspace_view = workspace_wall
window_view=grouped
motion = reduced
chrome = native
shelf = always
native_theme = tokyo-night
)");
    assert(preferences.workspaceView == WorkspaceViewPreference::WorkspaceWall);
    assert(preferences.windowView == WindowViewPreference::Grouped);
    assert(preferences.motion == MotionPreference::Reduced);
    assert(preferences.chrome == ChromePreference::Native);
    assert(preferences.shelf == ShelfPreference::Always);
    assert(preferences.nativeTheme == "tokyo-night");
}

void parsesQuattroPreferences() {
    const auto preferences = parsePreferences(R"(
workspace_view = carousel
window_view = deck
motion = quattro
)");
    assert(preferences.workspaceView == WorkspaceViewPreference::Carousel);
    assert(preferences.windowView == WindowViewPreference::Deck);
    assert(preferences.motion == MotionPreference::Quattro);
    assert(label(preferences.workspaceView) == "CAROUSEL");
    assert(label(preferences.windowView) == "DECK");
    assert(label(preferences.motion) == "SNAP");
}

void parsesRibbonWorkspaceView() {
    const auto preferences = parsePreferences("workspace_view = ribbon\n");
    assert(preferences.workspaceView == WorkspaceViewPreference::Ribbon);
    assert(label(preferences.workspaceView) == "RIBBON");
    assert(parsePreferences(serializePreferences(preferences)) == preferences);
}

void parsesDistinctAnimationProfiles() {
    assert(parsePreferences("motion = glitch\n").motion == MotionPreference::Cyberpunk);
    assert(parsePreferences("motion = lightcycle\n").motion == MotionPreference::Tron);
    assert(parsePreferences("motion = silk\n").motion == MotionPreference::Elegant);
    assert(label(MotionPreference::Quattro) == "SNAP");
    assert(label(MotionPreference::Cyberpunk) == "GLITCH");
    assert(label(MotionPreference::Tron) == "LIGHTCYCLE");
    assert(label(MotionPreference::Elegant) == "SILK");

    // Previously saved profile names remain valid after the UI rename.
    assert(parsePreferences("motion = quattro\n").motion == MotionPreference::Quattro);
    assert(parsePreferences("motion = cyberpunk\n").motion == MotionPreference::Cyberpunk);
    assert(parsePreferences("motion = tron\n").motion == MotionPreference::Tron);
    assert(parsePreferences("motion = elegant\n").motion == MotionPreference::Elegant);
}

void validatesNativeThemeSlugs() {
    assert(parsePreferences("native_theme = osaka-jade\n").nativeTheme == "osaka-jade");
    assert(parsePreferences("native_theme = solarized_dark\n").nativeTheme == "solarized_dark");
    assert(parsePreferences("native_theme = gruvbox.dark\n").nativeTheme == "gruvbox.dark");
    assert(parsePreferences("native_theme = current\n").nativeTheme == "current");
    assert(parsePreferences("native_theme = config\n").nativeTheme == "config");
    assert(parsePreferences("native_theme = auto\n").nativeTheme.empty());
    assert(parsePreferences("native_theme = .hidden\n").nativeTheme.empty());
    assert(parsePreferences("native_theme = ../../outside\n").nativeTheme.empty());
    assert(parsePreferences("native_theme = Tokyo-Night\n").nativeTheme.empty());
}

void parsesCustomizationPreferences() {
    assert(parsePreferences("chrome = radiant\n").chrome == ChromePreference::Radiant);
    assert(parsePreferences("chrome = native\n").chrome == ChromePreference::Native);
    assert(parsePreferences("chrome = flat\n").chrome == ChromePreference::Flat);
    assert(parsePreferences("chrome = surprise\n").chrome == ChromePreference::FollowConfig);
    assert(parsePreferences("shelf = auto\n").shelf == ShelfPreference::Auto);
    assert(parsePreferences("shelf = always\n").shelf == ShelfPreference::Always);
    assert(parsePreferences("shelf = hidden\n").shelf == ShelfPreference::Hidden);
    assert(parsePreferences("shelf = surprise\n").shelf == ShelfPreference::FollowConfig);
    assert(label(ChromePreference::FollowConfig) == "CONFIG");
    assert(label(ChromePreference::Radiant) == "Radiant");
    assert(label(ChromePreference::Native) == "Match desktop");
    assert(label(ChromePreference::Flat) == "Square");
    assert(label(ShelfPreference::Hidden) == "HIDDEN");
}

void ignoresUnknownKeysAndFallsBackOnUnknownValues() {
    const auto preferences = parsePreferences(R"(
unknown = preserved-nowhere
workspace_view = surprise
window_view = surprise
accent = surprise
motion = surprise
)");
    assert(preferences == PreferencesState{});
}

void ignoresLegacyAccentPreference() {
    const auto preferences = parsePreferences("accent = green\n");
    assert(preferences == PreferencesState{});
    assert(!serializePreferences(preferences).contains("accent ="));
}

void serializationRoundTrips() {
    const PreferencesState expected{
        .workspaceView = WorkspaceViewPreference::Carousel,
        .windowView = WindowViewPreference::Deck,
        .motion      = MotionPreference::Quattro,
        .chrome      = ChromePreference::Flat,
        .shelf       = ShelfPreference::Hidden,
        .windowNavigation = WindowNavigationPreference::Spatial,
        .nativeTheme = "tokyo-night",
    };
    assert(parsePreferences(serializePreferences(expected)) == expected);
}

} // namespace

int main() {
    const PreferencesState original;
    assert(preferenceUpdate(original, original) == PreferenceUpdate::None);
    for (const auto preset : {ChromePreference::Radiant, ChromePreference::Native, ChromePreference::Flat}) {
        auto changed = original;
        changed.chrome = preset;
        assert(preferenceUpdate(original, changed) == PreferenceUpdate::Repaint);
        assert(preferenceUpdate(changed, changed) == PreferenceUpdate::None);
        assert(preferenceUpdate(changed, original) == PreferenceUpdate::Repaint);
    }
    auto changed = original;
    changed.shelf = ShelfPreference::Always;
    assert(preferenceUpdate(original, changed) == PreferenceUpdate::Repaint);
    changed = original;
    changed.windowNavigation = WindowNavigationPreference::Spatial;
    assert(preferenceUpdate(original, changed) == PreferenceUpdate::Repaint);
    changed             = original;
    changed.nativeTheme = "hackerman";
    assert(preferenceUpdate(original, changed) == PreferenceUpdate::Repaint);
    changed = original;
    changed.windowView = WindowViewPreference::Deck;
    assert(preferenceUpdate(original, changed) == PreferenceUpdate::RebuildLayout);
    changed = original;
    changed.workspaceView = WorkspaceViewPreference::Carousel;
    assert(preferenceUpdate(original, changed) == PreferenceUpdate::RebuildLayout);
    changed        = original;
    changed.motion = MotionPreference::Off;
    assert(preferenceUpdate(original, changed) == PreferenceUpdate::RebuildLayout);
    for (const auto value : {"config", "list", "spatial", "unknown"}) {
        const auto preferences = parsePreferences(std::string{"window_navigation = "} + value);
        const auto expected = std::string_view{value} == "list" ? WindowNavigationPreference::List
            : std::string_view{value} == "spatial" ? WindowNavigationPreference::Spatial : WindowNavigationPreference::FollowConfig;
        assert(preferences.windowNavigation == expected);
        assert(parsePreferences(serializePreferences(preferences)) == preferences);
    }
    assert(!usesSpatialNavigation(WindowNavigationPreference::FollowConfig, false));
    assert(usesSpatialNavigation(WindowNavigationPreference::FollowConfig, true));
    assert(!usesSpatialNavigation(WindowNavigationPreference::List, true));
    assert(usesSpatialNavigation(WindowNavigationPreference::Spatial, false));
    assert(preferenceSourceLabel(true) == "Uses your Hyprland configuration");
    assert(preferenceSourceLabel(false).starts_with("Saved choice overrides"));
    assert(preferenceSourceLabel(true, true).contains("Desktop style unavailable; using Square"));
    assert(preferenceSourceLabel(false, true).ends_with("(saved choice)"));
    defaultsFollowExistingConfig();
    parsesEveryPreference();
    parsesQuattroPreferences();
    parsesRibbonWorkspaceView();
    parsesDistinctAnimationProfiles();
    validatesNativeThemeSlugs();
    parsesCustomizationPreferences();
    ignoresUnknownKeysAndFallsBackOnUnknownValues();
    ignoresLegacyAccentPreference();
    serializationRoundTrips();
    std::cout << "PreferencesTest passed\n";
    return 0;
}

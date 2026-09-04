#include <hypr-radiant/overview/PreferencesPanelGeometry.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

void centersPreferredPanelOnLargeMonitor() {
    const auto frame = computePreferencesPanel({.width = 1920.0, .height = 1080.0});
    assert(frame.panel.width == 720.0);
    assert(frame.panel.height == 422.0);
    assert(frame.panel.x == 600.0);
    assert(frame.panel.y == 329.0);
}

void wallAndCarouselOmitWindowArrangement() {
    const auto frame = computePreferencesPanel({.width = 1920.0, .height = 1080.0}, false);
    assert(frame.panel.height == 302.0);
    assert(frame.rows.size() == 3);
    assert(frame.options.size() == 15);
    assert(std::ranges::none_of(frame.rows, [](const PreferenceRow& row) {
        return row.control == PreferenceControl::WindowView;
    }));
    assert(std::ranges::none_of(frame.options, [](const PreferenceOption& option) {
        return option.control == PreferenceControl::WindowView;
    }));
}

void staysInsideSmallMonitor() {
    const auto frame = computePreferencesPanel({.x = 100.0, .y = 50.0, .width = 500.0, .height = 360.0});
    assert(frame.panel.x >= 100.0);
    assert(frame.panel.y >= 50.0);
    assert(frame.panel.x + frame.panel.width <= 600.0);
    assert(frame.panel.y + frame.panel.height <= 410.0);
}

void identifiesEveryControl() {
    const auto frame = computePreferencesPanel({.width = 1280.0, .height = 720.0});
    assert(containsPreferencesPanel(frame, frame.panel.x, frame.panel.y));
    assert(containsPreferencesPanel(frame, frame.panel.x + frame.panel.width - 1.0,
        frame.panel.y + frame.panel.height - 1.0));
    assert(!containsPreferencesPanel(frame, frame.panel.x - 1.0, frame.panel.y));
    assert(!containsPreferencesPanel(frame, frame.panel.x, frame.panel.y + frame.panel.height));
    for (const auto& row : frame.rows) {
        assert(hitTestPreferencesPanel(frame, row.rect.x + 4.0, row.rect.y + 4.0).control == row.control);
    }
    for (const auto& option : frame.options) {
        const PreferenceHit expected{.control = option.control, .value = option.value};
        assert(hitTestPreferencesPanel(frame, option.rect.x + 2.0, option.rect.y + 2.0) == expected);
    }
    assert(hitTestPreferencesPanel(frame, frame.closeButton.x + 2.0, frame.closeButton.y + 2.0).control == PreferenceControl::Close);
    assert(hitTestPreferencesPanel(frame, frame.appExposeButton.x + 2.0, frame.appExposeButton.y + 2.0).control == PreferenceControl::AppExpose);
    assert(hitTestPreferencesPanel(frame, 0.0, 0.0).control == PreferenceControl::None);
}

void presentsInstalledThemesAsACompactSelector() {
    constexpr auto themeCount = 28;
    const auto frame = computePreferencesPanel({.width = 1920.0, .height = 1080.0}, true, themeCount);
    assert(frame.panel.width == 820.0);
    assert(frame.panel.height == 482.0);
    assert(frame.panel.x == 550.0);
    assert(frame.panel.y == 299.0);
    assert(frame.nativeThemesPane.width == 0.0);
    assert(frame.rows.size() == 6);
    assert(frame.rows.back().control == PreferenceControl::NativeTheme);
    assert(frame.appExposeButton.y > frame.rows.back().rect.y + frame.rows.back().rect.height);

    const auto nativeThemeCount = std::ranges::count_if(frame.options, [](const PreferenceOption& option) {
        return option.control == PreferenceControl::NativeTheme;
    });
    assert(nativeThemeCount == 3);
    const auto previousTheme = std::ranges::find_if(frame.options, [](const PreferenceOption& option) {
        return option.control == PreferenceControl::NativeTheme && option.value == 0;
    });
    const auto selectedTheme = std::ranges::find_if(frame.options, [](const PreferenceOption& option) {
        return option.control == PreferenceControl::NativeTheme && option.value == 1;
    });
    const auto nextTheme = std::ranges::find_if(frame.options, [](const PreferenceOption& option) {
        return option.control == PreferenceControl::NativeTheme && option.value == 2;
    });
    assert(previousTheme != frame.options.end());
    assert(selectedTheme != frame.options.end());
    assert(nextTheme != frame.options.end());
    assert(selectedTheme->rect.width > previousTheme->rect.width);
    assert(selectedTheme->rect.width > nextTheme->rect.width);
    assert(hitTestPreferencesPanel(frame, nextTheme->rect.x + 2.0, nextTheme->rect.y + 2.0) ==
           (PreferenceHit{.control = PreferenceControl::NativeTheme, .value = 2}));
}

void keepsStageRowsOrderedAndFitsSmallMonitors() {
    const auto frame = computePreferencesPanel({.width = 500.0, .height = 360.0}, true, 28);
    const std::array expected{
        PreferenceControl::WorkspaceView,
        PreferenceControl::WindowView,
        PreferenceControl::Shelf,
        PreferenceControl::Motion,
        PreferenceControl::Chrome,
        PreferenceControl::NativeTheme,
    };
    assert(frame.rows.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        assert(frame.rows[index].control == expected[index]);
    assert(frame.appExposeButton.y >= frame.rows.back().rect.y + frame.rows.back().rect.height);
    assert(frame.appExposeButton.y + frame.appExposeButton.height <= frame.panel.y + frame.panel.height);
}

} // namespace

int main() {
    centersPreferredPanelOnLargeMonitor();
    staysInsideSmallMonitor();
    wallAndCarouselOmitWindowArrangement();
    identifiesEveryControl();
    presentsInstalledThemesAsACompactSelector();
    keepsStageRowsOrderedAndFitsSmallMonitors();
    std::cout << "PreferencesPanelGeometryTest passed\n";
    return 0;
}

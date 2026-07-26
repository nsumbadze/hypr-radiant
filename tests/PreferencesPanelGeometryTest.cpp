#include <hypr-radiant/overview/PreferencesPanelGeometry.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

void centersPreferredPanelOnLargeMonitor() {
    const auto frame = computePreferencesPanel({.width = 1920.0, .height = 1080.0});
    assert(frame.panel.width == 720.0);
    assert(frame.panel.height == 390.0);
    assert(frame.panel.x == 600.0);
    assert(frame.panel.y == 345.0);
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

} // namespace

int main() {
    centersPreferredPanelOnLargeMonitor();
    staysInsideSmallMonitor();
    identifiesEveryControl();
    std::cout << "PreferencesPanelGeometryTest passed\n";
    return 0;
}

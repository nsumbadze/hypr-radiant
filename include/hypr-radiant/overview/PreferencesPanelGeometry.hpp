#pragma once

#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

#include <vector>

namespace hypr_radiant {

enum class PreferenceControl {
    None,
    WorkspaceView,
    WindowView,
    Shelf,
    WindowNavigation,
    Motion,
    Chrome,
    NativeTheme,
    AppExpose,
    Close,
};

struct PreferenceRow {
    PreferenceControl control = PreferenceControl::None;
    LayoutRect        rect;
};

struct PreferenceOption {
    PreferenceControl control = PreferenceControl::None;
    int               value   = -1;
    LayoutRect        rect;
};

struct PreferenceHit {
    PreferenceControl control = PreferenceControl::None;
    int               value   = -1;

    bool operator==(const PreferenceHit&) const = default;
};

struct PreferencesPanelFrame {
    LayoutRect                    panel;
    LayoutRect                    closeButton;
    std::vector<PreferenceRow>    rows;
    std::vector<PreferenceOption> options;
    LayoutRect                    settingsPane;
    LayoutRect                    nativeThemesPane;
    LayoutRect                    appExposeButton;
};

[[nodiscard]] PreferencesPanelFrame computePreferencesPanel(
    const LayoutRect& monitorBounds, bool includeWindowArrangement = true, int nativeThemeOptionCount = 0);
[[nodiscard]] bool                  containsPreferencesPanel(const PreferencesPanelFrame& frame, double x, double y);
[[nodiscard]] PreferenceHit         hitTestPreferencesPanel(const PreferencesPanelFrame& frame, double x, double y);

} // namespace hypr_radiant

#pragma once

#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

#include <array>

namespace hypr_radiant {

enum class PreferenceControl {
    None,
    WorkspaceView,
    WindowView,
    Accent,
    Motion,
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
    LayoutRect                   panel;
    LayoutRect                   closeButton;
    std::array<PreferenceRow, 4> rows;
    std::array<PreferenceOption, 11> options;
    LayoutRect                   appExposeButton;
};

[[nodiscard]] PreferencesPanelFrame computePreferencesPanel(const LayoutRect& monitorBounds);
[[nodiscard]] PreferenceHit         hitTestPreferencesPanel(const PreferencesPanelFrame& frame, double x, double y);

} // namespace hypr_radiant

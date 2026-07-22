#pragma once

#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <cstdint>

namespace hypr_radiant {

enum class OverviewTargetType {
    None,
    Workspace,
    Window,
    NewWorkspace,
    CloseWindow,
};

struct OverviewTarget {
    OverviewTargetType type        = OverviewTargetType::None;
    std::int64_t       workspaceId = -1;
    std::uint64_t      windowId    = 0;
    std::int64_t       monitorId   = -1;
};

enum class NavigationDirection {
    Left,
    Right,
    Up,
    Down,
};

[[nodiscard]] RadiantPoint mapGlobalPointToFrame(
    const LayoutRect& globalBounds,
    const LayoutRect& frameBounds,
    double globalX,
    double globalY) noexcept;

/// Hotspot for the close affordance in a stage window card's top-left corner. Returns an empty
/// rect when the card is too small to carry one without burying the preview underneath it.
/// Hit testing and rendering both derive the button from this, so they cannot drift apart.
[[nodiscard]] LayoutRect closeButtonRect(const LayoutRect& windowRect) noexcept;

class HitTester {
  public:
    [[nodiscard]] OverviewTarget hitTest(const WorkspaceWallFrame& frame, double x, double y) const;
    [[nodiscard]] OverviewTarget initialSelection(const WorkspaceWallFrame& frame) const;
    [[nodiscard]] OverviewTarget moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction) const;
};

} // namespace hypr_radiant

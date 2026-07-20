#pragma once

#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <cstdint>

namespace hypr_radiant {

enum class OverviewTargetType {
    None,
    Workspace,
    Window,
    NewWorkspace,
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

class HitTester {
  public:
    [[nodiscard]] OverviewTarget hitTest(const WorkspaceWallFrame& frame, double x, double y) const;
    [[nodiscard]] OverviewTarget initialSelection(const WorkspaceWallFrame& frame) const;
    [[nodiscard]] OverviewTarget moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction) const;
};

} // namespace hypr_radiant

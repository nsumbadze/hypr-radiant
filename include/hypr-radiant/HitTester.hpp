#pragma once

#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <cstdint>

namespace hypr_radiant {

enum class OverviewTargetType {
    None,
    Workspace,
    Window,
};

struct OverviewTarget {
    OverviewTargetType type        = OverviewTargetType::None;
    std::int64_t       workspaceId = -1;
    std::uint64_t      windowId    = 0;
};

enum class NavigationDirection {
    Left,
    Right,
    Up,
    Down,
};

class HitTester {
  public:
    [[nodiscard]] OverviewTarget hitTest(const WorkspaceWallFrame& frame, double x, double y) const;
    [[nodiscard]] OverviewTarget initialSelection(const WorkspaceWallFrame& frame) const;
    [[nodiscard]] OverviewTarget moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction) const;
};

} // namespace hypr_radiant

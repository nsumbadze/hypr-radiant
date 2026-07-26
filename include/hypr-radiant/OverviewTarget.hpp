#pragma once

#include <cstdint>

namespace hypr_radiant {

enum class OverviewTargetType {
    None,
    Workspace,
    Window,
    Application,
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

// Identity by what the user is pointing at, deliberately ignoring monitorId so the same window or
// workspace compares equal regardless of which frame produced the target.
[[nodiscard]] inline bool sameTarget(const OverviewTarget& lhs, const OverviewTarget& rhs) {
    return lhs.type == rhs.type && lhs.workspaceId == rhs.workspaceId && lhs.windowId == rhs.windowId;
}

} // namespace hypr_radiant

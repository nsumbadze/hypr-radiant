#pragma once

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

} // namespace hypr_radiant

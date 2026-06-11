#pragma once

#include <hypr-radiant/RadiantState.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace hypr_radiant {

struct LayoutRect {
    double x      = 0.0;
    double y      = 0.0;
    double width  = 0.0;
    double height = 0.0;
};

struct WindowCard {
    std::uint64_t stableId    = 0;
    std::int64_t  workspaceId = -1;
    LayoutRect    rect;
    std::string   label;
    bool          focused     = false;
};

struct WorkspaceCard {
    std::int64_t            workspaceId = -1;
    std::string             name;
    LayoutRect              rect;
    std::vector<WindowCard> windows;
    bool                    active      = false;
    bool                    empty       = true;
};

struct WorkspaceWallFrame {
    std::int64_t               monitorId = -1;
    LayoutRect                 bounds;
    std::vector<WorkspaceCard> workspaces;
};

struct WorkspaceWallOptions {
    int    minimumWorkspaceSlots = 6;
    double outerPadding          = 112.0;
    double cardGap               = 24.0;
    double windowGap             = 10.0;
    double windowInset           = 22.0;
    bool   focusedStage          = false;
};

class WorkspaceWallLayout {
  public:
    [[nodiscard]] WorkspaceWallFrame compute(
        const RadiantState& state,
        const MonitorSnapshot& monitor,
        const RadiantSize& renderSize,
        const WorkspaceWallOptions& options = {}) const;
};

} // namespace hypr_radiant

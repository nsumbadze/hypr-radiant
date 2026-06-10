#include <hypr-radiant/WorkspaceWallLayout.hpp>

namespace hypr_radiant {

WorkspaceWallFrame WorkspaceWallLayout::compute(
    const RadiantState&,
    const MonitorSnapshot& monitor,
    const RadiantSize& renderSize,
    const WorkspaceWallOptions&) const {
    return {
        .monitorId = monitor.id,
        .bounds    = {.x = 0.0, .y = 0.0, .width = renderSize.width, .height = renderSize.height},
    };
}

} // namespace hypr_radiant

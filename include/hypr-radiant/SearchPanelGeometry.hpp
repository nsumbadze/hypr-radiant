#pragma once

#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <cstddef>

namespace hypr_radiant {

struct SearchPanelGeometry {
    double panelX = 0.0;
    double panelY = 0.0;
    double panelW = 0.0;
    double panelH = 0.0;
    double inputX = 0.0;
    double inputY = 0.0;
    double inputW = 0.0;
    double inputH = 0.0;
    double resultsY = 0.0;
    double rowHeight = 0.0;
    double rowGap = 0.0;
    std::size_t capacity = 0;
};

[[nodiscard]] SearchPanelGeometry computeSearchPanelGeometry(const WorkspaceWallFrame& frame, std::size_t resultCount);

} // namespace hypr_radiant

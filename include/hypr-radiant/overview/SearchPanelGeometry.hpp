#pragma once

#include <hypr-radiant/OverviewTarget.hpp>
#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

#include <cstddef>
#include <vector>

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

/// Index of the selected target in the result list, or 0 when it is absent.
[[nodiscard]] std::size_t selectedSearchIndex(const std::vector<OverviewTarget>& targets, const OverviewTarget& selected);

/// First result row to draw so the selected one stays within a window of `capacity` rows.
[[nodiscard]] std::size_t visibleSearchStart(const std::vector<OverviewTarget>& targets, const OverviewTarget& selected, std::size_t capacity);

} // namespace hypr_radiant

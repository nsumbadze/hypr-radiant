#include <hypr-radiant/SearchPanelGeometry.hpp>

#include <algorithm>
#include <cmath>

namespace hypr_radiant {
namespace {

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
}

} // namespace

SearchPanelGeometry computeSearchPanelGeometry(const WorkspaceWallFrame& frame, std::size_t resultCount) {
    const auto panelWidth     = std::min(640.0, std::max(1.0, frame.bounds.width - 96.0));
    const auto maxPanelHeight = std::min(540.0, std::max(1.0, frame.bounds.height - 96.0));
    const auto rowHeight      = 56.0;
    const auto rowGap         = 6.0;
    const auto inputHeight    = 48.0;
    const auto inputInset     = 20.0;
    const auto inputTopOffset = 20.0;
    const auto fixedHeight    = inputTopOffset + inputHeight + 20.0 + 20.0 + 20.0;
    const auto maxCapacity    = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::max(0.0, maxPanelHeight - fixedHeight + rowGap) / (rowHeight + rowGap)));
    const auto capacity   = std::max<std::size_t>(1, std::min(resultCount, maxCapacity));
    const auto rowsHeight = resultCount == 0 ? 0.0 :
        static_cast<double>(capacity) * rowHeight + static_cast<double>(capacity - 1) * rowGap;
    const auto panelHeight = std::min(maxPanelHeight, std::max(160.0, fixedHeight + rowsHeight));
    const auto panelX      = centered(frame.bounds.width, panelWidth);
    const auto panelY      = centered(frame.bounds.height, panelHeight) - 24.0;
    const auto inputY      = panelY + inputTopOffset;
    const auto resultsY    = inputY + inputHeight + 20.0;

    return {
        .panelX    = panelX,
        .panelY    = panelY,
        .panelW    = panelWidth,
        .panelH    = panelHeight,
        .inputX    = panelX + inputInset,
        .inputY    = inputY,
        .inputW    = std::max(1.0, panelWidth - inputInset * 2.0),
        .inputH    = inputHeight,
        .resultsY  = resultsY,
        .rowHeight = rowHeight,
        .rowGap    = rowGap,
        .capacity  = capacity,
    };
}

} // namespace hypr_radiant

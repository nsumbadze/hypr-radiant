#include <hypr-radiant/overview/SearchPanelGeometry.hpp>

#include <algorithm>
#include <cmath>

namespace hypr_radiant {
namespace {

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
}

} // namespace

SearchPanelGeometry computeSearchPanelGeometry(const WorkspaceWallFrame& frame, std::size_t resultCount) {
    const auto panelWidth     = std::min(680.0, std::max(1.0, frame.bounds.width - 48.0));
    const auto maxPanelHeight = std::min(420.0, std::max(1.0, frame.bounds.height - 96.0));
    const auto rowHeight      = std::min(56.0, std::max(1.0, maxPanelHeight * 0.25));
    const auto rowGap         = std::min(6.0, std::max(0.0, maxPanelHeight * 0.02));
    const auto inputInset     = std::min(20.0, std::max(0.0, (panelWidth - 1.0) / 2.0));
    const auto inputTopOffset = std::min(20.0, std::max(0.0, maxPanelHeight * 0.08));
    const auto inputHeight    = std::min(48.0, std::max(1.0, maxPanelHeight - inputTopOffset * 2.0));
    const auto resultsGap     = std::min(20.0, std::max(0.0, maxPanelHeight * 0.06));
    const auto footerHeight   = std::min(40.0, std::max(0.0, maxPanelHeight * 0.12));
    const auto fixedHeight    = inputTopOffset + inputHeight + resultsGap + footerHeight;
    const auto maxCapacity    = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::max(0.0, maxPanelHeight - fixedHeight + rowGap) / (rowHeight + rowGap)));
    const auto capacity = std::max<std::size_t>(1, std::min({resultCount, maxCapacity, std::size_t{6}}));
    // Empty search still needs a real result row. Collapsing to fixedHeight placed both empty-state
    // lines on top of the footer and let the second line wrap beyond the panel.
    const auto rowsHeight = resultCount == 0 ? 72.0 :
        static_cast<double>(capacity) * rowHeight + static_cast<double>(capacity - 1) * rowGap;
    const auto panelHeight = std::min(maxPanelHeight, std::max(std::min(136.0, maxPanelHeight), fixedHeight + rowsHeight));
    const auto panelX   = centered(frame.bounds.width, panelWidth);
    const auto preferredY = frame.bounds.height * 0.35 - panelHeight / 2.0;
    const auto panelY   = std::clamp(preferredY, std::min(48.0, centered(frame.bounds.height, panelHeight)), std::max(0.0, frame.bounds.height - panelHeight - std::min(48.0, centered(frame.bounds.height, panelHeight))));
    const auto inputY   = panelY + inputTopOffset;
    const auto resultsY = inputY + inputHeight + resultsGap;

    return {
        .panelX = panelX,
        .panelY = panelY,
        .panelW = panelWidth,
        .panelH = panelHeight,
        .inputX = panelX + inputInset,
        .inputY = inputY,
        .inputW    = std::max(1.0, panelWidth - inputInset * 2.0),
        .inputH = inputHeight,
        .resultsY  = resultsY,
        .rowHeight = rowHeight,
        .rowGap    = rowGap,
        .capacity  = capacity,
    };
}

std::size_t selectedSearchIndex(const std::vector<OverviewTarget>& targets, const OverviewTarget& selected) {
    const auto found = std::ranges::find_if(targets, [&selected](const OverviewTarget& target) { return sameTarget(target, selected); });
    return found == targets.end() ? 0 : static_cast<std::size_t>(std::distance(targets.begin(), found));
}

std::size_t visibleSearchStart(const std::vector<OverviewTarget>& targets, const OverviewTarget& selected, std::size_t capacity) {
    if (targets.size() <= capacity)
        return 0;

    const auto selectedIndex = selectedSearchIndex(targets, selected);
    if (selectedIndex < capacity)
        return 0;

    return std::min(selectedIndex - capacity + 1, targets.size() - capacity);
}

} // namespace hypr_radiant

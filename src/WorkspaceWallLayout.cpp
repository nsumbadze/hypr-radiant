#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace hypr_radiant {
namespace {

bool containsPositiveWorkspaceId(const WorkspaceSnapshot& workspace) {
    return workspace.id > 0 && !workspace.special;
}

std::string labelFor(const WindowSnapshot& window) {
    if (!window.title.empty())
        return window.title;

    if (!window.className.empty())
        return window.className;

    return "Window";
}

LayoutRect inset(const LayoutRect& rect, double amount) {
    return {.x = rect.x + amount, .y = rect.y + amount, .width = std::max(0.0, rect.width - amount * 2.0), .height = std::max(0.0, rect.height - amount * 2.0)};
}

} // namespace

WorkspaceWallFrame WorkspaceWallLayout::compute(
    const RadiantState& state,
    const MonitorSnapshot& monitor,
    const RadiantSize& renderSize,
    const WorkspaceWallOptions& options) const {
    WorkspaceWallFrame frame{
        .monitorId   = monitor.id,
        .bounds      = {.x = 0.0, .y = 0.0, .width = renderSize.width, .height = renderSize.height},
        .workspaces  = {},
    };

    int maxWorkspaceId = std::max(1, options.minimumWorkspaceSlots);
    std::map<std::int64_t, WorkspaceSnapshot> workspaceById;

    for (const auto& workspace : state.workspaces) {
        if (!containsPositiveWorkspaceId(workspace))
            continue;

        if (workspace.monitorId != monitor.id && workspace.monitorId != -1)
            continue;

        workspaceById[workspace.id] = workspace;
        maxWorkspaceId = std::max(maxWorkspaceId, static_cast<int>(workspace.id));
    }

    const auto count = static_cast<std::size_t>(maxWorkspaceId);
    const auto cols  = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
    const auto rows  = std::max(1, static_cast<int>(std::ceil(static_cast<double>(count) / static_cast<double>(cols))));

    const auto gridWidth  = std::max(1.0, renderSize.width - options.outerPadding * 2.0);
    const auto gridHeight = std::max(1.0, renderSize.height - options.outerPadding * 2.0);
    const auto cardWidth  = std::max(0.0, gridWidth - options.cardGap * static_cast<double>(cols - 1)) / static_cast<double>(cols);
    const auto cardHeight = std::max(0.0, gridHeight - options.cardGap * static_cast<double>(rows - 1)) / static_cast<double>(rows);

    for (int id = 1; id <= maxWorkspaceId; ++id) {
        const auto index = id - 1;
        const auto row   = index / cols;
        const auto col   = index % cols;

        const auto found = workspaceById.find(id);
        const auto name  = found != workspaceById.end() && !found->second.name.empty() ? found->second.name : std::to_string(id);

        WorkspaceCard card{
            .workspaceId = id,
            .name        = name,
            .rect        = {
                .x = options.outerPadding + static_cast<double>(col) * (cardWidth + options.cardGap),
                .y = options.outerPadding + static_cast<double>(row) * (cardHeight + options.cardGap),
                .width = cardWidth,
                .height = cardHeight,
            },
            .windows = {},
            .active = monitor.activeWorkspaceId == id,
            .empty  = true,
        };

        const auto inner = inset(card.rect, 18.0);
        std::vector<WindowSnapshot> windows;
        for (const auto& window : state.windows) {
            if (!window.mapped || window.workspaceId != id)
                continue;

            if (window.monitorId != monitor.id && window.monitorId != -1)
                continue;

            windows.push_back(window);
        }
        std::sort(windows.begin(), windows.end(), [](const WindowSnapshot& lhs, const WindowSnapshot& rhs) {
            return lhs.stableId < rhs.stableId;
        });

        const auto windowCount = windows.size();
        if (windowCount > 0) {
            card.empty = false;
            const auto winCols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(windowCount)))));
            const auto winRows = std::max(1, static_cast<int>(std::ceil(static_cast<double>(windowCount) / static_cast<double>(winCols))));
            const auto winW    = std::max(0.0, inner.width - options.windowGap * static_cast<double>(winCols - 1)) / static_cast<double>(winCols);
            const auto winH    = std::max(0.0, inner.height - options.windowGap * static_cast<double>(winRows - 1)) / static_cast<double>(winRows);

            for (std::size_t i = 0; i < windows.size(); ++i) {
                const auto winRow = static_cast<int>(i) / winCols;
                const auto winCol = static_cast<int>(i) % winCols;
                card.windows.push_back({
                    .stableId    = windows[i].stableId,
                    .workspaceId = id,
                    .rect        = {
                        .x = inner.x + static_cast<double>(winCol) * (winW + options.windowGap),
                        .y = inner.y + static_cast<double>(winRow) * (winH + options.windowGap),
                        .width = winW,
                        .height = winH,
                    },
                    .label = labelFor(windows[i]),
                });
            }
        }

        frame.workspaces.push_back(std::move(card));
    }

    return frame;
}

} // namespace hypr_radiant

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

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
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

    if (options.focusedStage) {
        const auto windowCountFor = [&state, &monitor](std::int64_t workspaceId) {
            return std::ranges::count_if(state.windows, [&monitor, workspaceId](const WindowSnapshot& window) {
                return window.mapped && window.workspaceId == workspaceId &&
                    (window.monitorId == monitor.id || window.monitorId == -1);
            });
        };
        const auto activeId     = monitor.activeWorkspaceId > 0 ? monitor.activeWorkspaceId : 1;
        const auto edge         = std::clamp(renderSize.width * 0.045, 18.0, 72.0);
        const auto mainGap      = std::clamp(renderSize.width * 0.015, 16.0, 28.0);
        const auto dockHeight   = std::min(renderSize.height, std::clamp(renderSize.height * 0.10, 64.0, 104.0));
        const auto dockGap      = std::clamp(renderSize.height * 0.025, 20.0, 30.0);
        const auto primaryWidth = std::min(780.0, std::max(0.0, renderSize.width * 0.44));
        const auto maxPrimaryHeight =
            std::min(320.0, std::max(0.0, renderSize.height - dockHeight - dockGap - 160.0));
        const auto primaryHeight = std::min(
            maxPrimaryHeight,
            std::clamp(130.0 + static_cast<double>(windowCountFor(activeId)) * 78.0, 190.0, 320.0));
        const auto sideWidth = std::min(
            360.0,
            std::max(0.0, (renderSize.width - primaryWidth) / 2.0 - edge - mainGap));
        const auto sideHeightFor = [&windowCountFor, primaryHeight](std::int64_t workspaceId) {
            return std::min(
                primaryHeight,
                std::min(primaryHeight * 0.82, std::max(150.0, 110.0 + static_cast<double>(windowCountFor(workspaceId)) * 62.0)));
        };
        const auto leftHeight  = sideHeightFor(activeId - 1);
        const auto rightHeight = sideHeightFor(activeId + 1);

        std::vector<int> stripIds;
        for (int id = 1; id <= maxWorkspaceId; ++id) {
            if (id != activeId && id != activeId - 1 && id != activeId + 1)
                stripIds.push_back(id);
        }

        const auto stripCount = stripIds.size();
        const auto stripGap   = std::clamp(renderSize.width * 0.012, 12.0, 22.0);
        const auto stripWidth = stripCount == 0 ? 0.0 :
            std::min(190.0, std::max(0.0, (renderSize.width - edge * 2.0 - stripGap * static_cast<double>(stripCount - 1)) / static_cast<double>(stripCount)));
        const auto stripTotalWidth =
            static_cast<double>(stripCount) * stripWidth + static_cast<double>(stripCount > 0 ? stripCount - 1 : 0) * stripGap;
        const auto primaryX = centered(renderSize.width, primaryWidth);
        const auto stackHeight = primaryHeight + (stripCount > 0 ? dockGap + dockHeight : 0.0);
        const auto maxTop      = std::max(0.0, renderSize.height - stackHeight - 48.0);
        const auto top         = std::clamp(
            centered(renderSize.height, stackHeight) - 8.0,
            std::min(72.0, maxTop),
            maxTop);
        const auto dockY = top + primaryHeight + dockGap;

        for (int id = 1; id <= maxWorkspaceId; ++id) {
            const auto found = workspaceById.find(id);
            const auto name  = found != workspaceById.end() && !found->second.name.empty() ? found->second.name : std::to_string(id);

            LayoutRect rect;
            if (id == activeId) {
                rect = {.x = primaryX, .y = top, .width = primaryWidth, .height = primaryHeight};
            } else if (id == activeId - 1) {
                rect = {
                    .x = std::max(0.0, primaryX - mainGap - sideWidth),
                    .y = top + centered(primaryHeight, leftHeight),
                    .width = sideWidth,
                    .height = leftHeight,
                };
            } else if (id == activeId + 1) {
                rect = {
                    .x = std::min(renderSize.width, primaryX + primaryWidth + mainGap),
                    .y = top + centered(primaryHeight, rightHeight),
                    .width = sideWidth,
                    .height = rightHeight,
                };
            } else {
                const auto stripIndex = static_cast<std::size_t>(std::distance(stripIds.begin(), std::ranges::find(stripIds, id)));
                rect = {
                    .x = centered(renderSize.width, stripTotalWidth) + static_cast<double>(stripIndex) * (stripWidth + stripGap),
                    .y = dockY,
                    .width = stripWidth,
                    .height = dockHeight,
                };
            }

            WorkspaceCard card{.workspaceId = id, .name = name, .rect = rect, .windows = {}, .active = monitor.activeWorkspaceId == id, .empty = true};

            const auto compact = card.rect.height <= 120.0;
            const auto insetX  = compact ? 12.0 : std::min(options.windowInset, 22.0);
            const auto header  = compact ? 34.0 : 54.0;
            const auto bottom  = compact ? 12.0 : 20.0;
            const auto inner   = LayoutRect{
                .x      = card.rect.x + insetX,
                .y      = card.rect.y + std::min(header, card.rect.height),
                .width  = std::max(0.0, card.rect.width - insetX * 2.0),
                .height = std::max(0.0, card.rect.height - std::min(header, card.rect.height) - bottom),
            };
            std::vector<WindowSnapshot> windows;
            for (const auto& window : state.windows) {
                if (!window.mapped || window.workspaceId != id)
                    continue;

                if (window.monitorId != monitor.id && window.monitorId != -1)
                    continue;

                windows.push_back(window);
            }
            std::sort(windows.begin(), windows.end(), [](const WindowSnapshot& lhs, const WindowSnapshot& rhs) { return lhs.stableId < rhs.stableId; });

            if (!windows.empty()) {
                card.empty = false;
                const auto windowCount = windows.size();
                const auto winGap      = compact ? 6.0 : options.windowGap;
                const auto availableHeight =
                    std::max(0.0, inner.height - winGap * static_cast<double>(windowCount - 1));
                const auto winH = std::min(compact ? 46.0 : 68.0, availableHeight / static_cast<double>(windowCount));

                for (std::size_t i = 0; i < windows.size(); ++i) {
                    card.windows.push_back({
                        .stableId    = windows[i].stableId,
                        .workspaceId = id,
                        .rect        = {
                            .x = inner.x,
                            .y = inner.y + static_cast<double>(i) * (winH + winGap),
                            .width = inner.width,
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

        const auto inner = inset(card.rect, options.windowInset);
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

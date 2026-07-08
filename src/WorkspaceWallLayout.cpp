#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

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
        const auto activeId = monitor.activeWorkspaceId > 0 ? monitor.activeWorkspaceId : 1;

        std::set<std::int64_t> ids;
        for (const auto& workspace : state.workspaces) {
            if (containsPositiveWorkspaceId(workspace) && (workspace.monitorId == monitor.id || workspace.monitorId == -1))
                ids.insert(workspace.id);
        }
        if (!ids.contains(activeId))
            ids.insert(activeId);
        const auto nextId = activeId + 1;
        if (!ids.contains(nextId))
            ids.insert(nextId);

        const auto leftPad   = std::clamp(renderSize.width * 0.025, 32.0, 72.0);
        const auto rightPad  = leftPad;
        const auto topPad    = std::clamp(renderSize.height * 0.037, 32.0, 56.0);
        const auto bottomPad = std::clamp(renderSize.height * 0.052, 44.0, 72.0);
        const auto mainGap   = std::clamp(renderSize.width * 0.010, 14.0, 28.0);
        const auto dockGap   = std::clamp(renderSize.width * 0.005, 8.0, 16.0);
        const auto stageGap  = std::clamp(renderSize.height * 0.022, 18.0, 32.0);

        const auto contentWidth  = std::max(1.0, renderSize.width - leftPad - rightPad);
        const auto contentHeight = std::max(1.0, renderSize.height - topPad - bottomPad);
        const auto activeWidth   = std::min(720.0, renderSize.width * 0.375);
        const auto activeHeight  = std::max(320.0, std::min(500.0, contentHeight - stageGap - 64.0 - 24.0));
        const auto sideWidth     = std::min(400.0, std::max(0.0, (renderSize.width - activeWidth) / 2.0 - leftPad - rightPad - mainGap));
        const auto sideHeight    = std::min(420.0, std::max(240.0, activeHeight * 0.84));
        const auto dockHeight    = 64.0;

        std::vector<std::int64_t> mainIds;
        std::vector<std::int64_t> dockIds;
        for (const auto id : ids) {
            if (id == activeId || id == activeId - 1 || id == activeId + 1)
                mainIds.push_back(id);
            else
                dockIds.push_back(id);
        }
        std::sort(mainIds.begin(), mainIds.end());
        std::sort(dockIds.begin(), dockIds.end());

        const auto dockCount = dockIds.size();
        const auto dockWidth = dockCount == 0 ? 0.0 :
            std::min(120.0, std::max(0.0, (contentWidth - static_cast<double>(dockCount - 1) * dockGap) / static_cast<double>(dockCount)));
        const auto dockTotal = dockCount == 0 ? 0.0 :
            static_cast<double>(dockCount) * dockWidth + static_cast<double>(dockCount - 1) * dockGap;
        const auto activeX   = centered(renderSize.width, activeWidth);
        const auto stackH    = activeHeight + (dockCount > 0 ? stageGap + dockHeight : 0.0);
        const auto top       = topPad + centered(contentHeight, stackH);
        const auto dockY     = top + activeHeight + stageGap;
        const auto dockX     = centered(renderSize.width, dockTotal);

        std::map<std::int64_t, LayoutRect> rects;
        for (const auto id : mainIds) {
            LayoutRect rect;
            if (id == activeId) {
                rect = {activeX, top, activeWidth, activeHeight};
            } else if (id == activeId - 1) {
                rect = {
                    std::max(0.0, activeX - mainGap - sideWidth),
                    top + centered(activeHeight, sideHeight),
                    sideWidth,
                    sideHeight,
                };
            } else {
                rect = {
                    std::min(renderSize.width - sideWidth, activeX + activeWidth + mainGap),
                    top + centered(activeHeight, sideHeight),
                    sideWidth,
                    sideHeight,
                };
            }
            rects[id] = rect;
        }
        for (std::size_t i = 0; i < dockCount; ++i) {
            const auto id = dockIds[i];
            rects[id] = {
                dockX + static_cast<double>(i) * (dockWidth + dockGap),
                dockY,
                dockWidth,
                dockHeight,
            };
        }

        for (const auto& [id, rect] : rects) {
            const auto found = workspaceById.find(id);
            const auto name  = found != workspaceById.end() && !found->second.name.empty() ? found->second.name : std::to_string(id);
            WorkspaceCard card{
                .workspaceId = id,
                .name        = name,
                .rect        = rect,
                .windows     = {},
                .active      = monitor.activeWorkspaceId == id,
                .empty       = true,
            };

            const auto compact = rect.height <= 120.0;
            const auto insetX  = compact ? 8.0 : 16.0;
            const auto header  = compact ? 24.0 : 44.0;
            const auto bottom  = compact ? 8.0 : 16.0;
            const auto winGap  = compact ? 4.0 : 8.0;
            const auto inner   = LayoutRect{
                .x      = rect.x + insetX,
                .y      = rect.y + std::min(header, rect.height),
                .width  = std::max(0.0, rect.width - insetX * 2.0),
                .height = std::max(0.0, rect.height - std::min(header, rect.height) - bottom),
            };

            std::vector<WindowSnapshot> windows;
            for (const auto& window : state.windows) {
                if (!window.mapped || window.workspaceId != id)
                    continue;
                if (window.monitorId != monitor.id && window.monitorId != -1)
                    continue;
                windows.push_back(window);
            }
            std::sort(windows.begin(), windows.end(), [](const auto& lhs, const auto& rhs) { return lhs.stableId < rhs.stableId; });

            if (!windows.empty()) {
                card.empty = false;
                const auto winH = std::max(
                    compact ? 4.0 : 24.0,
                    (inner.height - winGap * static_cast<double>(windows.size() - 1)) / static_cast<double>(windows.size()));
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

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
        .rail         = {},
        .stage        = {},
        .focusedStage = options.focusedStage,
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
        std::int64_t globalMaxId = 0;
        for (const auto& workspace : state.workspaces) {
            if (containsPositiveWorkspaceId(workspace))
                globalMaxId = std::max(globalMaxId, workspace.id);
        }
        const auto nextId = globalMaxId + 1;
        if (!ids.contains(nextId))
            ids.insert(nextId);

        const std::vector<std::int64_t> orderedIds(ids.begin(), ids.end());
        const auto previewId = ids.contains(options.previewWorkspaceId) ? options.previewWorkspaceId : activeId;
        const auto previewIt = std::ranges::find(orderedIds, previewId);
        const auto previewIndex = previewIt == orderedIds.end() ? std::size_t{0} : static_cast<std::size_t>(std::distance(orderedIds.begin(), previewIt));

        const auto edgePad      = std::clamp(renderSize.width * 0.0125, 16.0, 32.0);
        const auto topPad       = std::clamp(renderSize.height * 0.022, 16.0, 32.0);
        const auto cardGap      = std::clamp(renderSize.width * 0.00625, 8.0, 14.0);
        const auto labelHeight  = std::clamp(renderSize.height * 0.030, 26.0, 34.0);
        const auto cardHeight   = std::clamp(renderSize.height * 0.145, 96.0, 168.0);
        const auto monitorWidth = std::max(1.0, monitor.geometry.size.width);
        const auto monitorHeight = std::max(1.0, monitor.geometry.size.height);
        const auto cardWidth    = cardHeight * monitorWidth / monitorHeight;
        const auto railInset    = 16.0;
        const auto railHeight   = cardHeight + labelHeight + railInset * 2.0;
        const auto totalWidth   = static_cast<double>(orderedIds.size()) * cardWidth +
            static_cast<double>(orderedIds.empty() ? 0 : orderedIds.size() - 1) * cardGap;
        const auto railWidth    = std::min(std::max(1.0, renderSize.width - edgePad * 2.0), totalWidth + railInset * 2.0);
        const auto railX        = centered(renderSize.width, railWidth);
        const auto contentLeft  = railX + railInset;
        const auto contentRight = railX + railWidth - railInset;
        const auto unclampedX   = renderSize.width / 2.0 - cardWidth / 2.0 - static_cast<double>(previewIndex) * (cardWidth + cardGap);
        const auto minimumX     = contentRight - totalWidth;
        const auto maximumX     = contentLeft;
        const auto cardsX       = totalWidth <= railWidth - railInset * 2.0 ? contentLeft + centered(railWidth - railInset * 2.0, totalWidth) :
            std::clamp(unclampedX, minimumX, maximumX);

        frame.rail = {
            .bounds        = {.x = railX, .y = topPad, .width = railWidth, .height = railHeight},
            .overflowLeft  = cardsX < contentLeft - 0.5,
            .overflowRight = cardsX + totalWidth > contentRight + 0.5,
        };

        const auto stageSidePad = std::clamp(renderSize.width * 0.033, 24.0, 72.0);
        const auto stageTop     = topPad + railHeight + std::clamp(renderSize.height * 0.042, 28.0, 48.0);
        const auto stageBottom  = std::clamp(renderSize.height * 0.065, 48.0, 72.0);
        const LayoutRect stageBounds{
            .x      = stageSidePad,
            .y      = std::min(stageTop, renderSize.height),
            .width  = std::max(0.0, renderSize.width - stageSidePad * 2.0),
            .height = std::max(0.0, renderSize.height - stageTop - stageBottom),
        };

        const auto windowsForWorkspace = [&](std::int64_t id) {
            std::vector<WindowSnapshot> windows;
            for (const auto& window : state.windows) {
                if (!window.mapped || window.workspaceId != id)
                    continue;
                if (window.monitorId != monitor.id && window.monitorId != -1)
                    continue;
                windows.push_back(window);
            }
            std::sort(windows.begin(), windows.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.geometry.position.y != rhs.geometry.position.y)
                    return lhs.geometry.position.y < rhs.geometry.position.y;
                if (lhs.geometry.position.x != rhs.geometry.position.x)
                    return lhs.geometry.position.x < rhs.geometry.position.x;
                return lhs.stableId < rhs.stableId;
            });
            return windows;
        };

        const auto mapWindow = [&](const WindowSnapshot& window, const LayoutRect& target) {
            const auto scale = std::min(target.width / monitorWidth, target.height / monitorHeight);
            const auto desktopWidth = monitorWidth * scale;
            const auto desktopHeight = monitorHeight * scale;
            const auto originX = target.x + centered(target.width, desktopWidth);
            const auto originY = target.y + centered(target.height, desktopHeight);
            const auto localX = window.geometry.position.x - monitor.geometry.position.x;
            const auto localY = window.geometry.position.y - monitor.geometry.position.y;
            return LayoutRect{
                .x      = originX + localX * scale,
                .y      = originY + localY * scale,
                .width  = std::max(1.0, window.geometry.size.width * scale),
                .height = std::max(1.0, window.geometry.size.height * scale),
            };
        };

        const auto cardForWindow = [&](const WindowSnapshot& window, const LayoutRect& target, bool groupStart = false) {
            return WindowCard{
                .stableId     = window.stableId,
                .workspaceId  = window.workspaceId,
                .rect         = target,
                .label        = labelFor(window),
                .appClass     = window.className.empty() ? "Application" : window.className,
                .floating     = window.floating,
                .fullscreen   = window.fullscreen,
                .appGroupStart = groupStart,
            };
        };

        for (std::size_t index = 0; index < orderedIds.size(); ++index) {
            const auto id = orderedIds[index];
            const LayoutRect rect{
                .x      = cardsX + static_cast<double>(index) * (cardWidth + cardGap),
                .y      = topPad + railInset,
                .width  = cardWidth,
                .height = cardHeight,
            };
            const auto found = workspaceById.find(id);
            const auto name  = found != workspaceById.end() && !found->second.name.empty() ? found->second.name : std::to_string(id);
            WorkspaceCard card{
                .workspaceId = id,
                .name        = name,
                .rect        = rect,
                .windows     = {},
                .active      = monitor.activeWorkspaceId == id,
                .empty       = true,
                .createTarget = id == nextId,
            };

            const auto windows = windowsForWorkspace(id);
            if (!windows.empty()) {
                card.empty = false;
                const auto inner = inset(rect, 4.0);
                for (const auto& window : windows) {
                    card.windows.push_back(cardForWindow(window, mapWindow(window, inner)));
                }
            }

            frame.workspaces.push_back(std::move(card));
        }

        const auto stageFound = workspaceById.find(previewId);
        frame.stage = {
            .workspaceId = previewId,
            .name        = stageFound != workspaceById.end() && !stageFound->second.name.empty() ? stageFound->second.name : std::to_string(previewId),
            .bounds = stageBounds,
            .windows     = {},
            .empty       = true,
        };
        auto stageWindows = windowsForWorkspace(previewId);
        if (options.mode == OverviewMode::AppExpose) {
            stageWindows.clear();
            for (const auto& window : state.windows) {
                if (!window.mapped || (window.monitorId != monitor.id && window.monitorId != -1))
                    continue;
                if (!options.applicationFilter.empty() && window.className != options.applicationFilter)
                    continue;
                stageWindows.push_back(window);
            }
        }

        if (options.mode != OverviewMode::Spatial) {
            std::sort(stageWindows.begin(), stageWindows.end(), [](const WindowSnapshot& lhs, const WindowSnapshot& rhs) {
                if (lhs.className != rhs.className)
                    return lhs.className < rhs.className;
                if (lhs.workspaceId != rhs.workspaceId)
                    return lhs.workspaceId < rhs.workspaceId;
                return lhs.stableId < rhs.stableId;
            });
        }

        if (options.mode == OverviewMode::AppExpose) {
            frame.stage.workspaceId = stageWindows.empty() ? previewId : stageWindows.front().workspaceId;
            frame.stage.name = options.applicationFilter.empty() ? "Application" : options.applicationFilter;
        }

        const auto gridRect = [&](std::size_t index, std::size_t count) {
            const auto columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
            const auto rows = std::max(1, static_cast<int>(std::ceil(static_cast<double>(count) / columns)));
            const auto gap = std::clamp(renderSize.width * 0.012, 14.0, 24.0);
            const auto header = options.mode == OverviewMode::Grouped ? 22.0 : 0.0;
            const auto width = std::max(1.0, stageBounds.width - gap * (columns - 1)) / columns;
            const auto height = std::max(1.0, stageBounds.height - gap * (rows - 1) - header * rows) / rows;
            const auto row = static_cast<int>(index) / columns;
            const auto col = static_cast<int>(index) % columns;
            return LayoutRect{
                .x = stageBounds.x + col * (width + gap),
                .y = stageBounds.y + row * (height + gap + header) + header,
                .width = width,
                .height = height,
            };
        };

        const auto spatialRect = [&](const WindowSnapshot& window, std::size_t index, std::size_t count) {
            const auto columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
            const auto rows = std::max(1, static_cast<int>(std::ceil(static_cast<double>(count) / columns)));
            const auto outerInset = std::clamp(std::min(stageBounds.width, stageBounds.height) * 0.035, 18.0, 40.0);
            const auto content = inset(stageBounds, outerInset);
            const auto gap = std::clamp(renderSize.width * 0.014, 18.0, 28.0);
            const auto labelReserve = renderSize.height >= 400.0 ? 34.0 : 0.0;
            const auto cellWidth = std::max(1.0, content.width - gap * static_cast<double>(columns - 1)) / columns;
            const auto cellHeight = std::max(1.0, content.height - gap * static_cast<double>(rows - 1)) / rows;
            const auto row = static_cast<int>(index) / columns;
            const auto column = static_cast<int>(index) % columns;
            const auto rowStart = static_cast<std::size_t>(row * columns);
            const auto itemsInRow = std::min(static_cast<std::size_t>(columns), count - rowStart);
            const auto rowWidth = static_cast<double>(itemsInRow) * cellWidth + static_cast<double>(itemsInRow - 1) * gap;
            const auto rowX = content.x + centered(content.width, rowWidth);
            const LayoutRect cell{
                .x = rowX + static_cast<double>(column) * (cellWidth + gap),
                .y = content.y + static_cast<double>(row) * (cellHeight + gap),
                .width = cellWidth,
                .height = cellHeight,
            };

            const auto density = count == 1 ? 0.76 : count == 2 ? 0.88 : 0.92;
            const auto availableWidth = std::max(1.0, cell.width * density);
            const auto availableHeight = std::max(1.0, (cell.height - labelReserve) * density);
            const auto sourceWidth = std::max(1.0, window.geometry.size.width);
            const auto sourceHeight = std::max(1.0, window.geometry.size.height);
            const auto sourceAspect = sourceWidth / sourceHeight;
            auto width = availableWidth;
            auto height = width / sourceAspect;
            if (height > availableHeight) {
                height = availableHeight;
                width = height * sourceAspect;
            }

            return LayoutRect{
                .x = cell.x + centered(cell.width, width),
                .y = cell.y + centered(std::max(1.0, cell.height - labelReserve), height),
                .width = width,
                .height = height,
            };
        };

        std::string previousClass;
        for (std::size_t index = 0; index < stageWindows.size(); ++index) {
            const auto& window = stageWindows[index];
            frame.stage.empty = false;
            const auto groupStart = options.mode == OverviewMode::Grouped && window.className != previousClass;
            const auto rect = options.mode == OverviewMode::Spatial ? spatialRect(window, index, stageWindows.size()) : gridRect(index, stageWindows.size());
            frame.stage.windows.push_back(cardForWindow(window, rect, groupStart));
            previousClass = window.className;
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
                    .appClass = windows[i].className.empty() ? "Application" : windows[i].className,
                    .floating = windows[i].floating,
                    .fullscreen = windows[i].fullscreen,
                });
            }
        }

        frame.workspaces.push_back(std::move(card));
    }

    return frame;
}

} // namespace hypr_radiant

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

// Titles and classes are set by the client, so their length is not ours to trust. They are laid out
// by Pango and used as a texture-cache key; an unbounded one turns into a multi-megabyte surface and
// a stall on first paint. Far more than this never fits a card anyway.
constexpr std::size_t maxLabelBytes = 256;

std::string truncatedLabel(const std::string& value) {
    if (value.size() <= maxLabelBytes)
        return value;

    // Step back off a UTF-8 continuation byte so the cut never splits a code point.
    auto end = maxLabelBytes;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80)
        --end;

    return value.substr(0, end);
}

std::string labelFor(const WindowSnapshot& window) {
    if (!window.title.empty())
        return truncatedLabel(window.title);

    if (!window.className.empty())
        return truncatedLabel(window.className);

    return "Window";
}

LayoutRect inset(const LayoutRect& rect, double amount) {
    return {.x = rect.x + amount, .y = rect.y + amount, .width = std::max(0.0, rect.width - amount * 2.0), .height = std::max(0.0, rect.height - amount * 2.0)};
}

// Workspace IDs come straight from the compositor and are user-chosen: `hyprctl dispatch workspace
// 5000000` is legal. Filling every slot up to the highest live ID would allocate a card per integer
// and rescan every window per card, so the fill is bounded. Real workspaces above the cap are still
// listed; only the empty slots between them stop being generated.
constexpr std::int64_t maxFilledSlots = 64;

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
}

double stableNoise(std::uint64_t value, std::uint64_t salt) {
    value ^= salt + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return static_cast<double>(value & 0xffffU) / 65535.0;
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

    std::int64_t maxWorkspaceId = std::max(1, options.minimumWorkspaceSlots);
    std::map<std::int64_t, WorkspaceSnapshot> workspaceById;

    for (const auto& workspace : state.workspaces) {
        if (!containsPositiveWorkspaceId(workspace))
            continue;

        if (workspace.monitorId != monitor.id && workspace.monitorId != -1)
            continue;

        workspaceById[workspace.id] = workspace;
        // Clamped rather than narrowed: a huge live ID must not become the slot count, and
        // static_cast<int> of one past INT_MAX is implementation defined.
        maxWorkspaceId = std::max(maxWorkspaceId, std::min<std::int64_t>(workspace.id, maxFilledSlots));
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
        // A hole in the numbering is still a workspace the user can switch to, so give it an empty
        // slot rather than closing the gap. Collapsing 1/2/4 into three adjacent cards both hid
        // that 3 was reachable and shifted every card sideways as workspaces emptied out.
        // Skip any slot another monitor already owns: that workspace is reachable on its own rail,
        // and offering it here would drag it off the screen it currently lives on.
        std::set<std::int64_t> ownedElsewhere;
        for (const auto& workspace : state.workspaces) {
            if (containsPositiveWorkspaceId(workspace) && workspace.monitorId != monitor.id && workspace.monitorId != -1)
                ownedElsewhere.insert(workspace.id);
        }
        const auto highestSlot = std::min<std::int64_t>(*ids.rbegin(), maxFilledSlots);
        for (std::int64_t id = 1; id <= highestSlot; ++id) {
            if (!ownedElsewhere.contains(id))
                ids.insert(id);
        }
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
        const auto cardHeight   = std::clamp(renderSize.height * 0.145, 96.0, 168.0);
        const auto monitorWidth = std::max(1.0, monitor.geometry.size.width);
        const auto monitorHeight = std::max(1.0, monitor.geometry.size.height);
        const auto cardWidth    = cardHeight * monitorWidth / monitorHeight;
        const auto railInset    = 12.0;
        const auto railHeight   = cardHeight + railInset * 2.0;
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

        const auto gridRect = [&](const WindowSnapshot& window, std::size_t index, std::size_t count) {
            const auto columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
            const auto rows = std::max(1, static_cast<int>(std::ceil(static_cast<double>(count) / columns)));
            const auto gap = std::clamp(renderSize.width * 0.012, 14.0, 24.0);
            const auto header = options.mode == OverviewMode::Grouped ? 22.0 : 0.0;
            const auto cellWidth = std::max(1.0, (stageBounds.width - gap * (columns - 1)) / columns);
            const auto cellHeight = std::max(1.0, (stageBounds.height - gap * (rows - 1) - header * rows) / rows);
            const auto row = static_cast<int>(index) / columns;
            const auto col = static_cast<int>(index) % columns;
            const LayoutRect cell{
                .x = stageBounds.x + col * (cellWidth + gap),
                .y = stageBounds.y + row * (cellHeight + gap + header) + header,
                .width = cellWidth,
                .height = cellHeight,
            };
            const auto sourceWidth = std::max(1.0, window.geometry.size.width);
            const auto sourceHeight = std::max(1.0, window.geometry.size.height);
            const auto sourceAspect = sourceWidth / sourceHeight;
            // Grouped and Exposé grids get a tighter fit than the spatial stage: these cells are
            // uniform, so the extra breathing room only read as wasted space.
            const auto availableWidth = std::max(1.0, cell.width * 0.985);
            const auto availableHeight = std::max(1.0, cell.height * 0.96);
            auto width = availableWidth;
            auto height = width / sourceAspect;
            if (height > availableHeight) {
                height = availableHeight;
                width = height * sourceAspect;
            }
            return LayoutRect{
                .x = cell.x + centered(cell.width, width),
                .y = cell.y + centered(cell.height, height),
                .width = width,
                .height = height,
            };
        };

        // Fits a window into a slot without cropping, centred, preserving its aspect ratio.
        const auto fittedRect = [](const WindowSnapshot& window, const LayoutRect& slot, double fill) {
            const auto sourceAspect = std::max(1.0, window.geometry.size.width) / std::max(1.0, window.geometry.size.height);
            const auto availableWidth = std::max(1.0, slot.width * fill);
            const auto availableHeight = std::max(1.0, slot.height * fill);
            auto width = availableWidth;
            auto height = width / sourceAspect;
            if (height > availableHeight) {
                height = availableHeight;
                width = height * sourceAspect;
            }
            return LayoutRect{
                .x = slot.x + centered(slot.width, width),
                .y = slot.y + centered(slot.height, height),
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
            const auto cellWidth = std::max(1.0, (content.width - gap * static_cast<double>(columns - 1)) / columns);
            const auto cellHeight = std::max(1.0, (content.height - gap * static_cast<double>(rows - 1)) / rows);
            const auto row = static_cast<int>(index) / columns;
            const auto column = static_cast<int>(index) % columns;
            const auto rowStart = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns);
            const auto itemsInRow = std::min(static_cast<std::size_t>(columns), count - rowStart);
            const auto rowWidth = static_cast<double>(itemsInRow) * cellWidth + static_cast<double>(itemsInRow - 1) * gap;
            const auto rowX = content.x + centered(content.width, rowWidth);
            const LayoutRect cell{
                .x = rowX + static_cast<double>(column) * (cellWidth + gap),
                .y = content.y + static_cast<double>(row) * (cellHeight + gap),
                .width = cellWidth,
                .height = cellHeight,
            };

            // Keep each window in a non-overlapping cell, but salt its scale and
            // position by stable ID. This produces the relaxed, spatial rhythm of
            // Expose without making targets jump between overview sessions.
            const auto scaleNoise = stableNoise(window.stableId, 0x51a7U);
            const auto xNoise = stableNoise(window.stableId, 0x8d31U);
            const auto yNoise = stableNoise(window.stableId, 0xc247U);
            const auto density = count == 1 ? 0.80 : 0.80 + scaleNoise * 0.14;
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

            const auto usableHeight = std::max(1.0, cell.height - labelReserve);
            const auto freeX = std::max(0.0, cell.width - width);
            const auto freeY = std::max(0.0, usableHeight - height);
            const auto wave = (static_cast<int>(index + static_cast<std::size_t>(window.stableId)) % 2 == 0 ? -1.0 : 1.0) *
                std::min(12.0, freeY * 0.24);
            const auto x = std::clamp(cell.x + centered(cell.width, width) + (xNoise - 0.5) * freeX * 0.72,
                cell.x, std::max(cell.x, cell.x + cell.width - width));
            const auto y = std::clamp(cell.y + centered(usableHeight, height) + (yNoise - 0.5) * freeY * 0.62 + wave,
                cell.y, std::max(cell.y, cell.y + usableHeight - height));

            return LayoutRect{
                .x = x,
                .y = y,
                .width = width,
                .height = height,
            };
        };

        // Grouped mode gives each application its own shelf: a titled container holding that app's
        // windows. That container is the thing Spatial never draws, so the two modes read apart at
        // a glance instead of differing only by how the same cards are scattered.
        if (options.mode == OverviewMode::Grouped && !stageWindows.empty()) {
            std::vector<std::pair<std::string, std::vector<const WindowSnapshot*>>> groups;
            for (const auto& window : stageWindows) {
                if (groups.empty() || groups.back().first != window.className)
                    groups.emplace_back(window.className, std::vector<const WindowSnapshot*>{});
                groups.back().second.push_back(&window);
            }

            const auto groupCount = groups.size();
            const auto columns    = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(groupCount)))));
            const auto rows       = std::max(1, static_cast<int>(std::ceil(static_cast<double>(groupCount) / columns)));

            // Grouping is carried entirely by proximity: the gap between applications is several
            // times the gap between one application's windows, so the clusters read on their own
            // without a container, tint, or header.
            const auto gap        = std::clamp(renderSize.width * 0.030, 52.0, 104.0);
            const auto innerGap   = std::clamp(renderSize.width * 0.0035, 6.0, 12.0);
            const auto cellWidth  = std::max(1.0, (stageBounds.width - gap * (columns - 1)) / columns);
            const auto cellHeight = std::max(1.0, (stageBounds.height - gap * (rows - 1)) / rows);

            for (std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
                const auto row = static_cast<int>(groupIndex) / columns;
                const auto col = static_cast<int>(groupIndex) % columns;
                const LayoutRect container{
                    .x      = stageBounds.x + col * (cellWidth + gap),
                    .y      = stageBounds.y + row * (cellHeight + gap),
                    .width  = cellWidth,
                    .height = cellHeight,
                };

                const auto& inner = container;

                const auto count        = groups[groupIndex].second.size();
                const auto innerColumns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
                const auto innerRows    = std::max(1, static_cast<int>(std::ceil(static_cast<double>(count) / innerColumns)));
                const auto slotWidth    = std::max(1.0, (inner.width - innerGap * (innerColumns - 1)) / innerColumns);
                const auto slotHeight   = std::max(1.0, (inner.height - innerGap * (innerRows - 1)) / innerRows);

                for (std::size_t i = 0; i < count; ++i) {
                    // Row/column are deliberately truncating integer division; keep them in int
                    // variables so the intent is explicit rather than buried in a float expression.
                    const auto index       = static_cast<int>(i);
                    const auto innerColumn = index % innerColumns;
                    const auto innerRow    = index / innerColumns;
                    const LayoutRect slot{
                        .x      = inner.x + innerColumn * (slotWidth + innerGap),
                        .y      = inner.y + innerRow * (slotHeight + innerGap),
                        .width  = slotWidth,
                        .height = slotHeight,
                    };
                    frame.stage.empty = false;
                    frame.stage.windows.push_back(cardForWindow(*groups[groupIndex].second[i], fittedRect(*groups[groupIndex].second[i], slot, 0.96), i == 0));
                }
            }

            return frame;
        }

        for (std::size_t index = 0; index < stageWindows.size(); ++index) {
            const auto& window = stageWindows[index];
            frame.stage.empty = false;
            const auto rect = options.mode == OverviewMode::Spatial ? spatialRect(window, index, stageWindows.size()) : gridRect(window, index, stageWindows.size());
            frame.stage.windows.push_back(cardForWindow(window, rect, false));
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

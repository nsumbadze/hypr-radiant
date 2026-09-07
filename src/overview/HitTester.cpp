#include <hypr-radiant/overview/HitTester.hpp>

#include <hypr-radiant/overview/OverlayGeometry.hpp>
#include <hypr-radiant/overview/StageTransform.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace hypr_radiant {
namespace {

bool selectable(const LayoutRect& rect) {
    return rect.width > 0.0 && rect.height > 0.0;
}

LayoutRect rectFor(const WorkspaceWallFrame& frame, OverviewTarget target) {
    if (frame.focusedStage && target.type == OverviewTargetType::Window) {
        const auto stageWindow = std::ranges::find_if(frame.stage.windows, [target](const WindowCard& card) {
            return card.stableId == target.windowId;
        });
        if (stageWindow != frame.stage.windows.end())
            return stageWindow->rect;
    }

    for (const auto& workspace : frame.workspaces) {
        if ((target.type == OverviewTargetType::Workspace || target.type == OverviewTargetType::NewWorkspace) && workspace.workspaceId == target.workspaceId)
            return workspace.rect;

        for (const auto& window : workspace.windows) {
            if (target.type == OverviewTargetType::Window && window.stableId == target.windowId)
                return window.rect;
        }
    }

    return {};
}

std::vector<OverviewTarget> workspaceTargets(const WorkspaceWallFrame& frame) {
    std::vector<OverviewTarget> targets;
    for (const auto& workspace : frame.workspaces) {
        if (selectable(workspace.rect))
            targets.push_back({
                .type = workspace.createTarget ? OverviewTargetType::NewWorkspace : OverviewTargetType::Workspace,
                .workspaceId = workspace.workspaceId,
                .monitorId = frame.monitorId,
            });
    }
    return targets;
}

std::vector<OverviewTarget> windowTargets(const WorkspaceWallFrame& frame, std::int64_t workspaceId) {
    std::vector<OverviewTarget> targets;
    const auto append = [&targets, &frame](const auto& windows) {
        for (const auto& window : windows) {
            if (selectable(window.rect))
                targets.push_back({.type = OverviewTargetType::Window, .workspaceId = window.workspaceId,
                    .windowId = window.stableId, .monitorId = frame.monitorId});
        }
    };
    if (frame.focusedStage && frame.stage.workspaceId == workspaceId) {
        append(frame.stage.windows);
        return targets;
    }
    const auto workspace = std::ranges::find(frame.workspaces, workspaceId, &WorkspaceCard::workspaceId);
    if (workspace != frame.workspaces.end())
        append(workspace->windows);
    return targets;
}

double centerX(const LayoutRect& rect) { return rect.x + rect.width / 2.0; }
double centerY(const LayoutRect& rect) { return rect.y + rect.height / 2.0; }

std::optional<OverviewTarget> nearestInDirection(const WorkspaceWallFrame& frame, OverviewTarget current,
    const std::vector<OverviewTarget>& candidates, NavigationDirection direction, bool preferAlignment = false) {
    const auto currentRect = rectFor(frame, current);
    if (!selectable(currentRect))
        return std::nullopt;
    const auto cx = centerX(currentRect);
    const auto cy = centerY(currentRect);
    std::optional<OverviewTarget> best;
    auto bestScore = 1.0e18;
    bool bestAligned = false;
    for (const auto& target : candidates) {
        if (target.type == current.type && target.workspaceId == current.workspaceId && target.windowId == current.windowId)
            continue;
        const auto rect = rectFor(frame, target);
        const auto dx = centerX(rect) - cx;
        const auto dy = centerY(rect) - cy;
        const auto inDirection =
            (direction == NavigationDirection::Left && dx < -1.0) ||
            (direction == NavigationDirection::Right && dx > 1.0) ||
            (direction == NavigationDirection::Up && dy < -1.0) ||
            (direction == NavigationDirection::Down && dy > 1.0);
        if (!inDirection)
            continue;
        const auto horizontal = direction == NavigationDirection::Left || direction == NavigationDirection::Right;
        const auto primary = horizontal ? std::abs(dx) : std::abs(dy);
        const auto secondary = horizontal ? std::abs(dy) : std::abs(dx);
        const auto aligned = horizontal
            ? std::min(rect.y + rect.height, currentRect.y + currentRect.height) > std::max(rect.y, currentRect.y)
            : std::min(rect.x + rect.width, currentRect.x + currentRect.width) > std::max(rect.x, currentRect.x);
        const auto score = preferAlignment ? dx * dx + dy * dy : primary * 1000.0 + secondary;
        if (!best || (preferAlignment && aligned != bestAligned ? aligned : score < bestScore)) {
            bestScore   = score;
            bestAligned = aligned;
            best        = target;
        }
    }
    return best;
}

} // namespace

RadiantPoint mapGlobalPointToFrame(
    const LayoutRect& globalBounds,
    const LayoutRect& frameBounds,
    double globalX,
    double globalY) noexcept {
    const auto scaleX = globalBounds.width > 0.0 ? frameBounds.width / globalBounds.width : 1.0;
    const auto scaleY = globalBounds.height > 0.0 ? frameBounds.height / globalBounds.height : 1.0;
    return {
        .x = frameBounds.x + (globalX - globalBounds.x) * scaleX,
        .y = frameBounds.y + (globalY - globalBounds.y) * scaleY,
    };
}

LayoutRect closeButtonRect(const LayoutRect& windowRect) noexcept {
    constexpr auto size    = 22.0;
    constexpr auto inset   = 8.0;
    // Below this the button would cover most of the thumbnail, so the card simply does not get one.
    constexpr auto minCard = 96.0;

    if (windowRect.width < minCard || windowRect.height < minCard)
        return {};

    return {.x = windowRect.x + windowRect.width - inset - size, .y = windowRect.y + inset, .width = size, .height = size};
}

OverviewTarget HitTester::hitTest(const WorkspaceWallFrame& frame, double x, double y) const {
    if (frame.focusedStage) {
        for (const auto& window : frame.stage.windows) {
            // Checked ahead of the card itself so the corner belongs to the button, not the window.
            if (contains(closeButtonRect(window.rect), x, y))
                return {
                    .type = OverviewTargetType::CloseWindow,
                    .workspaceId = window.workspaceId,
                    .windowId = window.stableId,
                    .monitorId = frame.monitorId,
                };
            if (contains(window.rect, x, y))
                return {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId, .monitorId = frame.monitorId};
        }

        if (contains(frame.rail.bounds, x, y)) {
            for (const auto& workspace : frame.workspaces) {
                if (contains(workspace.rect, x, y))
                    return {
                        .type = workspace.createTarget ? OverviewTargetType::NewWorkspace : OverviewTargetType::Workspace,
                        .workspaceId = workspace.workspaceId,
                        .monitorId = frame.monitorId,
                    };
            }
        }

        if (contains(frame.stage.bounds, x, y))
            return {.type = OverviewTargetType::Workspace, .workspaceId = frame.stage.workspaceId, .monitorId = frame.monitorId};

        return {};
    }

    for (const auto& workspace : frame.workspaces) {
        // Ribbon blades behave like Omarchy theme-picker slices: clicking one promotes the whole
        // workspace into the center. Only the expanded workspace exposes individual windows.
        if (frame.ribbon && workspace.workspaceId != frame.previewWorkspaceId)
            continue;
        for (const auto& window : workspace.windows) {
            if (contains(window.rect, x, y))
                return {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId, .monitorId = frame.monitorId};
        }
    }

    for (const auto& workspace : frame.workspaces) {
        if (contains(workspace.rect, x, y))
            return {
                .type = workspace.createTarget ? OverviewTargetType::NewWorkspace : OverviewTargetType::Workspace,
                .workspaceId = workspace.workspaceId,
                .monitorId = frame.monitorId,
            };
    }

    return {};
}

OverviewTarget HitTester::hitTestDisplayedStage(
    const WorkspaceWallFrame& frame, double x, double y, double shelfProgress) const {
    if (!frame.focusedStage)
        return hitTest(frame, x, y);

    const auto progress   = std::clamp(shelfProgress, 0.0, 1.0);
    const auto railOffset = stageRailEntranceOffset(frame, progress);
    auto       railBounds = frame.rail.bounds;
    railBounds.y += railOffset;
    if (contains(railBounds, x, y))
        return hitTest(frame, x, y - railOffset);

    const auto displayedStage = interpolatedRect(collapsedStageBounds(frame), frame.stage.bounds, progress);
    if (!contains(displayedStage, x, y))
        return {};

    if (const auto mapped = mapStagePointToSource(frame.stage.bounds, displayedStage, {.x = x, .y = y}))
        return hitTest(frame, mapped->x, mapped->y);

    // Aspect-preserving stage mapping can leave narrow letterbox margins. They are still visible
    // workspace background, so hovering them selects the current workspace rather than a hidden
    // rail card whose static layout happens to sit under the same coordinate.
    return {
        .type        = OverviewTargetType::Workspace,
        .workspaceId = frame.stage.workspaceId,
        .monitorId   = frame.monitorId,
    };
}

OverviewTarget HitTester::initialSelection(const WorkspaceWallFrame& frame) const {
    for (const auto& workspace : frame.workspaces) {
        if (workspace.active && selectable(workspace.rect))
            return {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId, .monitorId = frame.monitorId};
    }

    const auto targets = workspaceTargets(frame);
    if (!targets.empty())
        return targets.front();

    return {};
}

OverviewTarget HitTester::moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction,
    NavigationOptions options) const {
    const auto horizontal = direction == NavigationDirection::Left || direction == NavigationDirection::Right;

    if (options.spatialWindows && current.type == OverviewTargetType::Window) {
        const auto targets = windowTargets(frame, current.workspaceId);
        if (const auto nearest = nearestInDirection(frame, current, targets, direction, true))
            return *nearest;
        if (direction == NavigationDirection::Up)
            return {.type = OverviewTargetType::Workspace, .workspaceId = current.workspaceId, .monitorId = frame.monitorId};
        return current;
    }

    // Carousel cards are a logical sequence displayed as a centered hero with stacked side
    // columns. Resolve horizontal motion directly from that sequence: this avoids both ambiguous
    // geometry scoring and allocating a temporary target vector for every arrow or swipe.
    if (frame.carousel && horizontal) {
        const auto currentCard = std::ranges::find_if(frame.workspaces, [current](const WorkspaceCard& card) {
            return card.workspaceId == current.workspaceId;
        });
        if (currentCard == frame.workspaces.end())
            return initialSelection(frame);

        auto index = static_cast<std::size_t>(std::distance(frame.workspaces.begin(), currentCard));
        for (std::size_t attempts = 0; attempts < frame.workspaces.size(); ++attempts) {
            if (direction == NavigationDirection::Left)
                index = index == 0 ? frame.workspaces.size() - 1 : index - 1;
            else
                index = (index + 1) % frame.workspaces.size();

            const auto& card = frame.workspaces[index];
            if (selectable(card.rect))
                return {
                    .type = card.createTarget ? OverviewTargetType::NewWorkspace : OverviewTargetType::Workspace,
                    .workspaceId = card.workspaceId,
                    .monitorId = frame.monitorId,
                };
        }
        return current;
    }

    auto targets = workspaceTargets(frame);
    if (horizontal && !(frame.focusedStage && options.allShelfTargets)) {
        std::erase_if(targets, [](OverviewTarget target) { return target.type == OverviewTargetType::NewWorkspace; });
        // Stepping the rail should land on workspaces that actually hold something. Empty slots are
        // there so the numbering reads correctly, not as stops on the way past. Skipping them keeps
        // a sweep from stalling on gaps that have nothing to show on the stage. The workspace_wall
        // grid is left alone: every slot is a deliberate cell there, so skipping would strand
        // vertical neighbours behind an empty column.
        auto occupied = frame.focusedStage ? targets : std::vector<OverviewTarget>{};
        std::erase_if(occupied, [&frame](OverviewTarget target) {
            const auto card = std::ranges::find_if(frame.workspaces, [target](const WorkspaceCard& candidate) {
                return candidate.workspaceId == target.workspaceId;
            });
            return card != frame.workspaces.end() && card->empty;
        });
        // Everything empty means there is nothing to skip to, so leave the rail navigable.
        if (!occupied.empty())
            targets = std::move(occupied);
    }
    if (targets.empty())
        return {};

    if (current.type == OverviewTargetType::Workspace && direction == NavigationDirection::Down) {
        if (options.spatialWindows && options.returnWindow.workspaceId == current.workspaceId &&
            options.returnWindow.monitorId == frame.monitorId) {
            const auto windows = windowTargets(frame, current.workspaceId);
            const auto remembered = std::ranges::find_if(windows, [&](OverviewTarget target) {
                return target.windowId == options.returnWindow.windowId;
            });
            if (remembered != windows.end())
                return *remembered;
        }
        if (frame.focusedStage && current.workspaceId == frame.stage.workspaceId) {
            const auto window = std::ranges::find_if(frame.stage.windows, [](const WindowCard& card) { return selectable(card.rect); });
            if (window != frame.stage.windows.end())
                return {.type = OverviewTargetType::Window, .workspaceId = window->workspaceId, .windowId = window->stableId, .monitorId = frame.monitorId};
        }

        const auto workspace = std::ranges::find_if(frame.workspaces, [current](const WorkspaceCard& card) {
            return card.workspaceId == current.workspaceId;
        });
        if (workspace != frame.workspaces.end()) {
            const auto window = std::ranges::find_if(workspace->windows, [](const WindowCard& card) { return selectable(card.rect); });
            if (window != workspace->windows.end())
                return {.type = OverviewTargetType::Window, .workspaceId = window->workspaceId, .windowId = window->stableId, .monitorId = frame.monitorId};
        }
    }

    if (current.type == OverviewTargetType::Window) {
        if (frame.focusedStage) {
            const auto window = std::ranges::find_if(frame.stage.windows, [current](const WindowCard& card) {
                return card.stableId == current.windowId;
            });
            if (window != frame.stage.windows.end()) {
                if (direction == NavigationDirection::Down) {
                    const auto next = std::find_if(window + 1, frame.stage.windows.end(), [](const WindowCard& card) { return selectable(card.rect); });
                    if (next != frame.stage.windows.end())
                        return {.type = OverviewTargetType::Window, .workspaceId = next->workspaceId, .windowId = next->stableId, .monitorId = frame.monitorId};
                }
                if (direction == NavigationDirection::Up) {
                    auto previous = window;
                    while (previous != frame.stage.windows.begin()) {
                        --previous;
                        if (selectable(previous->rect))
                            return {.type = OverviewTargetType::Window, .workspaceId = previous->workspaceId, .windowId = previous->stableId, .monitorId = frame.monitorId};
                    }
                    return {.type = OverviewTargetType::Workspace, .workspaceId = frame.stage.workspaceId, .monitorId = frame.monitorId};
                }

                current = {.type = OverviewTargetType::Workspace, .workspaceId = frame.stage.workspaceId, .monitorId = frame.monitorId};
            }
        }

        for (const auto& workspace : frame.workspaces) {
            const auto window = std::ranges::find_if(workspace.windows, [current](const WindowCard& card) {
                return card.stableId == current.windowId;
            });
            if (window == workspace.windows.end())
                continue;

            if (direction == NavigationDirection::Down) {
                const auto next = std::find_if(window + 1, workspace.windows.end(), [](const WindowCard& card) { return selectable(card.rect); });
                if (next != workspace.windows.end())
                    return {.type = OverviewTargetType::Window, .workspaceId = next->workspaceId, .windowId = next->stableId, .monitorId = frame.monitorId};
            }
            if (direction == NavigationDirection::Up) {
                auto previous = window;
                while (previous != workspace.windows.begin()) {
                    --previous;
                    if (selectable(previous->rect))
                        return {.type = OverviewTargetType::Window, .workspaceId = previous->workspaceId, .windowId = previous->stableId, .monitorId = frame.monitorId};
                }
                return {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId};
            }

            current = {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId};
            break;
        }
    }

    const auto currentRect = rectFor(frame, current);
    if (current.type == OverviewTargetType::None || currentRect.width <= 0.0 || currentRect.height <= 0.0)
        return initialSelection(frame);

    if (const auto best = nearestInDirection(frame, current, targets, direction))
        return *best;

    if (horizontal) {
        const auto compareX = [&frame](OverviewTarget lhs, OverviewTarget rhs) {
            return centerX(rectFor(frame, lhs)) < centerX(rectFor(frame, rhs));
        };
        return direction == NavigationDirection::Left ? *std::ranges::max_element(targets, compareX) :
                                                        *std::ranges::min_element(targets, compareX);
    }

    return current;
}

OverviewTarget HitTester::cycleWindow(const WorkspaceWallFrame& frame, OverviewTarget current, int step) const {
    auto workspaceId = current.workspaceId;
    if (workspaceId <= 0 && frame.focusedStage)
        workspaceId = frame.stage.workspaceId;
    const auto targets = windowTargets(frame, workspaceId);
    if (targets.empty())
        return current;
    if (current.type != OverviewTargetType::Window)
        return step < 0 ? targets.back() : targets.front();
    const auto found = std::ranges::find_if(targets, [current](OverviewTarget target) { return target.windowId == current.windowId; });
    if (found == targets.end())
        return step < 0 ? targets.back() : targets.front();
    const auto index = static_cast<std::size_t>(std::distance(targets.begin(), found));
    return step < 0 ? targets[(index + targets.size() - 1) % targets.size()] : targets[(index + 1) % targets.size()];
}

} // namespace hypr_radiant

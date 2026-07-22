#include <hypr-radiant/HitTester.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace hypr_radiant {
namespace {

bool selectable(const LayoutRect& rect) {
    return rect.width > 0.0 && rect.height > 0.0;
}

bool contains(const LayoutRect& rect, double x, double y) {
    return selectable(rect) && x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
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

double centerX(const LayoutRect& rect) { return rect.x + rect.width / 2.0; }
double centerY(const LayoutRect& rect) { return rect.y + rect.height / 2.0; }

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

OverviewTarget HitTester::hitTest(const WorkspaceWallFrame& frame, double x, double y) const {
    if (frame.focusedStage) {
        for (const auto& window : frame.stage.windows) {
            if (contains(window.rect, x, y))
                return {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId};
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
            return {.type = OverviewTargetType::Workspace, .workspaceId = frame.stage.workspaceId};

        return {};
    }

    for (const auto& workspace : frame.workspaces) {
        for (const auto& window : workspace.windows) {
            if (contains(window.rect, x, y))
                return {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId};
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

OverviewTarget HitTester::moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction) const {
    auto targets = workspaceTargets(frame);
    const auto horizontal = direction == NavigationDirection::Left || direction == NavigationDirection::Right;
    if (horizontal) {
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
        if (frame.focusedStage && current.workspaceId == frame.stage.workspaceId) {
            const auto window = std::ranges::find_if(frame.stage.windows, [](const WindowCard& card) { return selectable(card.rect); });
            if (window != frame.stage.windows.end())
                return {.type = OverviewTargetType::Window, .workspaceId = window->workspaceId, .windowId = window->stableId};
        }

        const auto workspace = std::ranges::find_if(frame.workspaces, [current](const WorkspaceCard& card) {
            return card.workspaceId == current.workspaceId;
        });
        if (workspace != frame.workspaces.end()) {
            const auto window = std::ranges::find_if(workspace->windows, [](const WindowCard& card) { return selectable(card.rect); });
            if (window != workspace->windows.end())
                return {.type = OverviewTargetType::Window, .workspaceId = window->workspaceId, .windowId = window->stableId};
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
                        return {.type = OverviewTargetType::Window, .workspaceId = next->workspaceId, .windowId = next->stableId};
                }
                if (direction == NavigationDirection::Up) {
                    auto previous = window;
                    while (previous != frame.stage.windows.begin()) {
                        --previous;
                        if (selectable(previous->rect))
                            return {.type = OverviewTargetType::Window, .workspaceId = previous->workspaceId, .windowId = previous->stableId};
                    }
                    return {.type = OverviewTargetType::Workspace, .workspaceId = frame.stage.workspaceId};
                }

                current = {.type = OverviewTargetType::Workspace, .workspaceId = frame.stage.workspaceId};
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
                    return {.type = OverviewTargetType::Window, .workspaceId = next->workspaceId, .windowId = next->stableId};
            }
            if (direction == NavigationDirection::Up) {
                auto previous = window;
                while (previous != workspace.windows.begin()) {
                    --previous;
                    if (selectable(previous->rect))
                        return {.type = OverviewTargetType::Window, .workspaceId = previous->workspaceId, .windowId = previous->stableId};
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

    const auto cx = centerX(currentRect);
    const auto cy = centerY(currentRect);

    auto best      = current;
    auto bestScore = 1.0e18;

    for (const auto& target : targets) {
        if (target.type == current.type && target.workspaceId == current.workspaceId && target.windowId == current.windowId)
            continue;

        const auto rect = rectFor(frame, target);
        const auto dx   = centerX(rect) - cx;
        const auto dy   = centerY(rect) - cy;

        const auto inDirection =
            (direction == NavigationDirection::Left && dx < -1.0) ||
            (direction == NavigationDirection::Right && dx > 1.0) ||
            (direction == NavigationDirection::Up && dy < -1.0) ||
            (direction == NavigationDirection::Down && dy > 1.0);

        if (!inDirection)
            continue;

        const auto primary   = direction == NavigationDirection::Left || direction == NavigationDirection::Right ? std::abs(dx) : std::abs(dy);
        const auto secondary = direction == NavigationDirection::Left || direction == NavigationDirection::Right ? std::abs(dy) : std::abs(dx);
        const auto score     = primary * 1000.0 + secondary;
        if (score < bestScore) {
            bestScore = score;
            best      = target;
        }
    }

    if (bestScore == 1.0e18 && horizontal) {
        const auto compareX = [&frame](OverviewTarget lhs, OverviewTarget rhs) {
            return centerX(rectFor(frame, lhs)) < centerX(rectFor(frame, rhs));
        };
        return direction == NavigationDirection::Left ? *std::ranges::max_element(targets, compareX) :
                                                        *std::ranges::min_element(targets, compareX);
    }

    return best;
}

} // namespace hypr_radiant

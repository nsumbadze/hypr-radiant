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
    for (const auto& workspace : frame.workspaces) {
        if (target.type == OverviewTargetType::Workspace && workspace.workspaceId == target.workspaceId)
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
            targets.push_back({.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId});
    }
    return targets;
}

double centerX(const LayoutRect& rect) { return rect.x + rect.width / 2.0; }
double centerY(const LayoutRect& rect) { return rect.y + rect.height / 2.0; }

} // namespace

OverviewTarget HitTester::hitTest(const WorkspaceWallFrame& frame, double x, double y) const {
    for (const auto& workspace : frame.workspaces) {
        for (const auto& window : workspace.windows) {
            if (contains(window.rect, x, y))
                return {.type = OverviewTargetType::Window, .workspaceId = window.workspaceId, .windowId = window.stableId};
        }
    }

    for (const auto& workspace : frame.workspaces) {
        if (contains(workspace.rect, x, y))
            return {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId};
    }

    return {};
}

OverviewTarget HitTester::initialSelection(const WorkspaceWallFrame& frame) const {
    for (const auto& workspace : frame.workspaces) {
        if (workspace.active && selectable(workspace.rect))
            return {.type = OverviewTargetType::Workspace, .workspaceId = workspace.workspaceId};
    }

    const auto targets = workspaceTargets(frame);
    if (!targets.empty())
        return targets.front();

    return {};
}

OverviewTarget HitTester::moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction) const {
    const auto targets = workspaceTargets(frame);
    if (targets.empty())
        return {};

    if (current.type == OverviewTargetType::Workspace && direction == NavigationDirection::Down) {
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

    return best;
}

} // namespace hypr_radiant

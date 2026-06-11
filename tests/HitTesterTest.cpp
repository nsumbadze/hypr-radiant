#include <hypr-radiant/HitTester.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

WorkspaceWallFrame frame() {
    WorkspaceWallFrame frame{.monitorId = 1, .bounds = {.width = 400, .height = 300}};
    frame.workspaces.push_back({.workspaceId = 1, .name = "1", .rect = {.x = 10, .y = 10, .width = 180, .height = 120}, .active = true, .empty = false});
    frame.workspaces.back().windows.push_back({.stableId = 11, .workspaceId = 1, .rect = {.x = 30, .y = 30, .width = 80, .height = 50}, .label = "A"});
    frame.workspaces.push_back({.workspaceId = 2, .name = "2", .rect = {.x = 210, .y = 10, .width = 180, .height = 120}, .empty = true});
    frame.workspaces.push_back({.workspaceId = 3, .name = "3", .rect = {.x = 10, .y = 150, .width = 180, .height = 120}, .empty = true});
    return frame;
}

void windowHitWinsOverWorkspaceHit() {
    const auto target = HitTester{}.hitTest(frame(), 40, 40);
    assert(target.type == OverviewTargetType::Window);
    assert(target.workspaceId == 1);
    assert(target.windowId == 11);
}

void hoverInsideWindowReturnsWindowTarget() {
    const auto target = HitTester{}.hitTest(frame(), 109, 79);
    assert(target.type == OverviewTargetType::Window);
    assert(target.workspaceId == 1);
    assert(target.windowId == 11);
}

void workspaceBackgroundHitWorks() {
    const auto target = HitTester{}.hitTest(frame(), 220, 40);
    assert(target.type == OverviewTargetType::Workspace);
    assert(target.workspaceId == 2);
}

void hoverOutsideAllCardsReturnsNoTarget() {
    const auto target = HitTester{}.hitTest(frame(), 200, 140);
    assert(target.type == OverviewTargetType::None);
}

void navigationMovesThroughWorkspaceGrid() {
    const auto target = HitTester{}.moveSelection(frame(), {.type = OverviewTargetType::Workspace, .workspaceId = 1}, NavigationDirection::Right);
    assert(target.type == OverviewTargetType::Workspace);
    assert(target.workspaceId == 2);
}

void windowCurrentNavigationMovesFromContainingWorkspace() {
    const auto target = HitTester{}.moveSelection(frame(), {.type = OverviewTargetType::Window, .workspaceId = 1, .windowId = 11}, NavigationDirection::Right);
    assert(target.type == OverviewTargetType::Workspace);
    assert(target.workspaceId == 2);
}

void downEntersWorkspaceWindowsAndUpReturns() {
    auto testFrame = frame();
    testFrame.workspaces.front().windows.push_back(
        {.stableId = 12, .workspaceId = 1, .rect = {.x = 30, .y = 85, .width = 80, .height = 35}, .label = "B"});

    const auto firstWindow = HitTester{}.moveSelection(testFrame, {.type = OverviewTargetType::Workspace, .workspaceId = 1}, NavigationDirection::Down);
    assert(firstWindow.type == OverviewTargetType::Window);
    assert(firstWindow.windowId == 11);

    const auto secondWindow = HitTester{}.moveSelection(testFrame, firstWindow, NavigationDirection::Down);
    assert(secondWindow.type == OverviewTargetType::Window);
    assert(secondWindow.windowId == 12);

    const auto previousWindow = HitTester{}.moveSelection(testFrame, secondWindow, NavigationDirection::Up);
    assert(previousWindow.type == OverviewTargetType::Window);
    assert(previousWindow.windowId == 11);

    const auto workspace = HitTester{}.moveSelection(testFrame, previousWindow, NavigationDirection::Up);
    assert(workspace.type == OverviewTargetType::Workspace);
    assert(workspace.workspaceId == 1);
}

void rightAndBottomEdgesAreOutsideHitBounds() {
    const auto rightEdge = HitTester{}.hitTest(frame(), 390, 40);
    assert(rightEdge.type == OverviewTargetType::None);

    const auto bottomEdge = HitTester{}.hitTest(frame(), 220, 130);
    assert(bottomEdge.type == OverviewTargetType::None);
}

void zeroSizedRectsAreNotHittable() {
    WorkspaceWallFrame zeroFrame{.monitorId = 1, .bounds = {.width = 100, .height = 100}};
    zeroFrame.workspaces.push_back({.workspaceId = 1, .name = "1", .rect = {.x = 10, .y = 10, .width = 0, .height = 50}, .empty = false});
    zeroFrame.workspaces.back().windows.push_back({.stableId = 11, .workspaceId = 1, .rect = {.x = 10, .y = 10, .width = 50, .height = 0}, .label = "A"});

    const auto target = HitTester{}.hitTest(zeroFrame, 10, 10);
    assert(target.type == OverviewTargetType::None);
}

void emptyFrameHasNoInitialSelection() {
    const auto target = HitTester{}.initialSelection({});
    assert(target.type == OverviewTargetType::None);
}

void missingCurrentFallsBackToInitialSelection() {
    const auto target = HitTester{}.moveSelection(frame(), {.type = OverviewTargetType::Workspace, .workspaceId = 99}, NavigationDirection::Right);
    assert(target.type == OverviewTargetType::Workspace);
    assert(target.workspaceId == 1);
}

} // namespace

int main() {
    windowHitWinsOverWorkspaceHit();
    hoverInsideWindowReturnsWindowTarget();
    workspaceBackgroundHitWorks();
    hoverOutsideAllCardsReturnsNoTarget();
    navigationMovesThroughWorkspaceGrid();
    windowCurrentNavigationMovesFromContainingWorkspace();
    downEntersWorkspaceWindowsAndUpReturns();
    rightAndBottomEdgesAreOutsideHitBounds();
    zeroSizedRectsAreNotHittable();
    emptyFrameHasNoInitialSelection();
    missingCurrentFallsBackToInitialSelection();
    std::cout << "HitTesterTest passed\n";
    return 0;
}

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

WorkspaceWallFrame focusedFrame() {
    WorkspaceWallFrame frame{
        .monitorId = 1,
        .bounds = {.width = 800, .height = 600},
        .focusedStage = true,
    };
    frame.rail.bounds = {.x = 40, .y = 20, .width = 720, .height = 160};
    frame.workspaces.push_back({.workspaceId = 1, .name = "dev", .rect = {.x = 60, .y = 40, .width = 200, .height = 112}, .active = true, .empty = false});
    frame.workspaces.back().windows.push_back({.stableId = 11, .workspaceId = 1, .rect = {.x = 70, .y = 50, .width = 80, .height = 60}, .label = "thumbnail"});
    frame.workspaces.push_back({.workspaceId = 2, .name = "web", .rect = {.x = 280, .y = 40, .width = 200, .height = 112}, .empty = false});
    frame.stage = {
        .workspaceId = 1,
        .name = "dev",
        .bounds = {.x = 40, .y = 210, .width = 720, .height = 330},
        .windows = {
            {.stableId = 11, .workspaceId = 1, .rect = {.x = 70, .y = 240, .width = 240, .height = 180}, .label = "Editor"},
            {.stableId = 12, .workspaceId = 1, .rect = {.x = 340, .y = 260, .width = 300, .height = 220}, .label = "Browser"},
        },
        .empty = false,
    };
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

void navigationSkipsZeroSizedWorkspaceTargets() {
    WorkspaceWallFrame testFrame{.monitorId = 1, .bounds = {.width = 400, .height = 120}};
    testFrame.workspaces.push_back({.workspaceId = 1, .name = "1", .rect = {.x = 10, .y = 10, .width = 100, .height = 80}, .active = true});
    testFrame.workspaces.push_back({.workspaceId = 2, .name = "2", .rect = {.x = 150, .y = 10, .width = 0, .height = 80}});
    testFrame.workspaces.push_back({.workspaceId = 3, .name = "3", .rect = {.x = 250, .y = 10, .width = 100, .height = 80}});

    const auto target = HitTester{}.moveSelection(
        testFrame,
        {.type = OverviewTargetType::Workspace, .workspaceId = 1},
        NavigationDirection::Right);

    assert(target.type == OverviewTargetType::Workspace);
    assert(target.workspaceId == 3);
}

void navigationSkipsZeroSizedWindowTargets() {
    WorkspaceWallFrame testFrame{.monitorId = 1, .bounds = {.width = 220, .height = 180}};
    testFrame.workspaces.push_back({.workspaceId = 1, .name = "1", .rect = {.x = 10, .y = 10, .width = 180, .height = 140}, .active = true});
    testFrame.workspaces.back().windows.push_back({.stableId = 11, .workspaceId = 1, .rect = {.x = 30, .y = 40, .width = 120, .height = 0}, .label = "Hidden"});
    testFrame.workspaces.back().windows.push_back({.stableId = 12, .workspaceId = 1, .rect = {.x = 30, .y = 70, .width = 120, .height = 40}, .label = "Visible"});

    const auto target = HitTester{}.moveSelection(
        testFrame,
        {.type = OverviewTargetType::Workspace, .workspaceId = 1},
        NavigationDirection::Down);

    assert(target.type == OverviewTargetType::Window);
    assert(target.windowId == 12);
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

void focusedRailTreatsMiniaturesAsWorkspaceTargets() {
    const auto target = HitTester{}.hitTest(focusedFrame(), 80, 60);
    assert(target.type == OverviewTargetType::Workspace);
    assert(target.workspaceId == 1);
}

void createCardHasDedicatedTarget() {
    auto testFrame = focusedFrame();
    testFrame.workspaces.at(1).createTarget = true;

    const auto target = HitTester{}.hitTest(testFrame, 300, 60);

    assert(target.type == OverviewTargetType::NewWorkspace);
    assert(target.workspaceId == 2);
    assert(target.monitorId == testFrame.monitorId);
}

void focusedStageWindowsAreInteractive() {
    const auto target = HitTester{}.hitTest(focusedFrame(), 100, 280);
    assert(target.type == OverviewTargetType::Window);
    assert(target.windowId == 11);
}

void focusedNavigationEntersStageAndReturnsToRail() {
    const auto testFrame = focusedFrame();
    const auto first = HitTester{}.moveSelection(testFrame, {.type = OverviewTargetType::Workspace, .workspaceId = 1}, NavigationDirection::Down);
    assert(first.type == OverviewTargetType::Window);
    assert(first.windowId == 11);

    const auto second = HitTester{}.moveSelection(testFrame, first, NavigationDirection::Down);
    assert(second.type == OverviewTargetType::Window);
    assert(second.windowId == 12);

    const auto workspace = HitTester{}.moveSelection(testFrame, first, NavigationDirection::Up);
    assert(workspace.type == OverviewTargetType::Workspace);
    assert(workspace.workspaceId == 1);
}

void horizontalWorkspaceNavigationWrapsAndSkipsCreateTarget() {
    auto testFrame = focusedFrame();
    testFrame.workspaces.push_back({
        .workspaceId = 3,
        .name = "new",
        .rect = {.x = 500, .y = 40, .width = 200, .height = 112},
        .createTarget = true,
    });

    const auto previous = HitTester{}.moveSelection(
        testFrame, {.type = OverviewTargetType::Workspace, .workspaceId = 1}, NavigationDirection::Left);
    assert(previous.type == OverviewTargetType::Workspace);
    assert(previous.workspaceId == 2);

    const auto next = HitTester{}.moveSelection(
        testFrame, {.type = OverviewTargetType::Workspace, .workspaceId = 2}, NavigationDirection::Right);
    assert(next.type == OverviewTargetType::Workspace);
    assert(next.workspaceId == 1);
}

void horizontalWorkspaceNavigationSkipsEmptyWorkspaces() {
    // Ctrl+wheel, the horizontal three-finger swipe and the arrow keys all step the rail through
    // this path. Filled gap slots are rendered so the numbering reads correctly, but sweeping
    // should carry past them to the next workspace that actually holds something.
    WorkspaceWallFrame testFrame{.monitorId = 1, .bounds = {.width = 600, .height = 200}, .focusedStage = true};
    testFrame.workspaces.push_back({.workspaceId = 1, .name = "1", .rect = {.x = 10, .y = 10, .width = 100, .height = 80}, .active = true, .empty = false});
    testFrame.workspaces.push_back({.workspaceId = 2, .name = "2", .rect = {.x = 150, .y = 10, .width = 100, .height = 80}});
    testFrame.workspaces.push_back({.workspaceId = 3, .name = "3", .rect = {.x = 290, .y = 10, .width = 100, .height = 80}, .empty = false});

    const auto next = HitTester{}.moveSelection(
        testFrame, {.type = OverviewTargetType::Workspace, .workspaceId = 1}, NavigationDirection::Right);
    assert(next.type == OverviewTargetType::Workspace);
    assert(next.workspaceId == 3);

    const auto back = HitTester{}.moveSelection(
        testFrame, {.type = OverviewTargetType::Workspace, .workspaceId = 3}, NavigationDirection::Left);
    assert(back.type == OverviewTargetType::Workspace);
    assert(back.workspaceId == 1);
}

void horizontalNavigationStillMovesWhenEveryWorkspaceIsEmpty() {
    // Skipping empties must not strand the selection when there is nothing else to reach for.
    WorkspaceWallFrame testFrame{.monitorId = 1, .bounds = {.width = 600, .height = 200}, .focusedStage = true};
    testFrame.workspaces.push_back({.workspaceId = 1, .name = "1", .rect = {.x = 10, .y = 10, .width = 100, .height = 80}, .active = true});
    testFrame.workspaces.push_back({.workspaceId = 2, .name = "2", .rect = {.x = 150, .y = 10, .width = 100, .height = 80}});

    const auto next = HitTester{}.moveSelection(
        testFrame, {.type = OverviewTargetType::Workspace, .workspaceId = 1}, NavigationDirection::Right);
    assert(next.type == OverviewTargetType::Workspace);
    assert(next.workspaceId == 2);
}

void scaledGlobalPointerMapsToRenderCoordinates() {
    const auto point = mapGlobalPointToFrame(
        {.x = 100.0, .y = 50.0, .width = 1280.0, .height = 800.0},
        {.x = 0.0, .y = 0.0, .width = 1920.0, .height = 1200.0},
        1100.0,
        650.0);

    assert(point.x == 1500.0);
    assert(point.y == 900.0);
}

void unscaledGlobalPointerOnlyRemovesMonitorOrigin() {
    const auto point = mapGlobalPointToFrame(
        {.x = 1920.0, .y = 0.0, .width = 1920.0, .height = 1080.0},
        {.x = 0.0, .y = 0.0, .width = 1920.0, .height = 1080.0},
        2880.0,
        540.0);

    assert(point.x == 960.0);
    assert(point.y == 540.0);
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
    navigationSkipsZeroSizedWorkspaceTargets();
    navigationSkipsZeroSizedWindowTargets();
    emptyFrameHasNoInitialSelection();
    missingCurrentFallsBackToInitialSelection();
    focusedRailTreatsMiniaturesAsWorkspaceTargets();
    createCardHasDedicatedTarget();
    focusedStageWindowsAreInteractive();
    focusedNavigationEntersStageAndReturnsToRail();
    horizontalWorkspaceNavigationWrapsAndSkipsCreateTarget();
    horizontalWorkspaceNavigationSkipsEmptyWorkspaces();
    horizontalNavigationStillMovesWhenEveryWorkspaceIsEmpty();
    scaledGlobalPointerMapsToRenderCoordinates();
    unscaledGlobalPointerOnlyRemovesMonitorOrigin();
    std::cout << "HitTesterTest passed\n";
    return 0;
}

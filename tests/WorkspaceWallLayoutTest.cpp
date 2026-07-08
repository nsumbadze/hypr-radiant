#include <hypr-radiant/SearchPanelGeometry.hpp>
#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

using namespace hypr_radiant;

namespace {

RadiantState sampleState() {
    RadiantState state;
    state.monitors.push_back({.id = 1, .name = "DP-1", .geometry = {.size = {.width = 1920, .height = 1080}}, .activeWorkspaceId = 2, .activeWorkspaceName = "2"});
    state.workspaces.push_back({.id = 1, .name = "1", .monitorId = 1, .monitorName = "DP-1"});
    state.workspaces.push_back({.id = 2, .name = "2", .monitorId = 1, .monitorName = "DP-1", .visible = true});
    state.windows.push_back({.stableId = 10, .title = "Editor", .className = "Code", .workspaceId = 2, .monitorId = 1, .mapped = true});
    return state;
}

WorkspaceWallOptions stageOptions() {
    return WorkspaceWallOptions{
        .outerPadding = 48.0,
        .cardGap      = 20.0,
        .windowGap    = 8.0,
        .windowInset  = 16.0,
        .focusedStage = true,
    };
}

void gridLayoutFillsMinimumSlots() {
    const auto state = sampleState();
    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080});

    assert(frame.monitorId == 1);
    assert(frame.workspaces.size() == 6);
    assert(frame.workspaces.at(1).workspaceId == 2);
    assert(frame.workspaces.at(1).active);
    assert(frame.workspaces.at(1).windows.size() == 1);
    assert(frame.workspaces.at(1).windows.front().label == "Editor");
}

void polishedDefaultsLeaveBreathingRoom() {
    const auto state = sampleState();
    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080});

    assert(frame.workspaces.front().rect.x >= 100.0);
    assert(frame.workspaces.front().rect.y >= 100.0);
    assert(frame.workspaces.at(1).windows.size() == 1);
    assert(frame.workspaces.at(1).windows.front().rect.x > frame.workspaces.at(1).rect.x + 20.0);
    assert(frame.workspaces.at(1).windows.front().rect.y > frame.workspaces.at(1).rect.y + 20.0);
}

void focusedStageUsesRealWorkspacesPlusEmptyNext() {
    const auto state = sampleState();
    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, stageOptions());

    assert(frame.workspaces.size() == 3);
    assert(frame.workspaces.at(0).workspaceId == 1);
    assert(frame.workspaces.at(1).workspaceId == 2);
    assert(frame.workspaces.at(2).workspaceId == 3);
    assert(frame.workspaces.at(0).empty);
    assert(frame.workspaces.at(2).empty);
    assert(!frame.workspaces.at(1).empty);
    assert(frame.workspaces.at(1).active);
}

void focusedStageDoesNotGeneratePhantomWorkspacesForGaps() {
    // Regression for B3: the old minimumWorkspaceSlots logic generated cards
    // for every ID from 1..maxWorkspaceId, producing phantom workspaces when
    // real IDs had gaps. The focused-stage branch must emit only real
    // workspaces for the monitor plus one empty "next" workspace.
    RadiantState state;
    state.monitors.push_back({.id = 1, .name = "DP-1", .geometry = {.size = {.width = 1920, .height = 1080}}, .activeWorkspaceId = 5, .activeWorkspaceName = "5"});
    state.workspaces.push_back({.id = 1, .name = "one", .monitorId = 1, .monitorName = "DP-1"});
    state.workspaces.push_back({.id = 5, .name = "five", .monitorId = 1, .monitorName = "DP-1", .visible = true});

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, stageOptions());

    assert(frame.workspaces.size() == 3);
    assert(frame.workspaces.at(0).workspaceId == 1);
    assert(frame.workspaces.at(1).workspaceId == 5);
    assert(frame.workspaces.at(2).workspaceId == 6);
    assert(frame.workspaces.at(0).empty);
    assert(frame.workspaces.at(1).empty);
    assert(frame.workspaces.at(2).empty);
    assert(frame.workspaces.at(1).active);
}

void focusedStageCardSpacingMatchesSpec() {
    const auto state = sampleState();
    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, stageOptions());

    assert(frame.workspaces.size() == 3);

    const auto& left   = frame.workspaces.at(0);
    const auto& active = frame.workspaces.at(1);
    const auto& right  = frame.workspaces.at(2);

    const auto leftPad   = std::clamp(1920.0 * 0.025, 32.0, 72.0);
    const auto topPad    = std::clamp(1080.0 * 0.037, 32.0, 56.0);
    const auto bottomPad = std::clamp(1080.0 * 0.052, 44.0, 72.0);
    const auto mainGap   = std::clamp(1920.0 * 0.010, 14.0, 28.0);
    const auto stageGap  = std::clamp(1080.0 * 0.022, 18.0, 32.0);
    const auto activeW   = std::min(720.0, 1920.0 * 0.375);
    const auto activeH   = std::max(320.0, std::min(500.0, 1080.0 - topPad - bottomPad - stageGap - 64.0 - 24.0));
    const auto sideW     = std::min(400.0, std::max(0.0, (1920.0 - activeW) / 2.0 - leftPad * 2.0 - mainGap));
    const auto sideH     = std::min(420.0, std::max(240.0, activeH * 0.84));
    const auto activeX   = (1920.0 - activeW) / 2.0;

    assert(active.rect.width == activeW);
    assert(active.rect.height == activeH);
    assert(std::abs(active.rect.x - activeX) < 0.5);
    assert(std::abs(active.rect.x + active.rect.width / 2.0 - 960.0) < 0.5);

    assert(left.rect.width == sideW);
    assert(left.rect.height == sideH);
    assert(std::abs(left.rect.x - (activeX - mainGap - sideW)) < 0.5);
    assert(std::abs((active.rect.x - left.rect.x - left.rect.width) - mainGap) < 0.5);

    assert(right.rect.width == sideW);
    assert(right.rect.height == sideH);
    assert(std::abs(right.rect.x - (activeX + activeW + mainGap)) < 0.5);
    assert(std::abs((right.rect.x - active.rect.x - active.rect.width) - mainGap) < 0.5);

    assert(left.rect.y > topPad);
    assert(active.rect.y > topPad);
    assert(active.rect.y + active.rect.height + stageGap + 64.0 <= 1080.0 - bottomPad);
}

void focusedStageEmptyNextWorkspaceGeometry() {
    RadiantState state;
    state.monitors.push_back({.id = 1, .name = "DP-1", .geometry = {.size = {.width = 1920, .height = 1080}}, .activeWorkspaceId = 3, .activeWorkspaceName = "3"});
    state.workspaces.push_back({.id = 1, .name = "1", .monitorId = 1, .monitorName = "DP-1"});
    state.workspaces.push_back({.id = 2, .name = "2", .monitorId = 1, .monitorName = "DP-1"});
    state.workspaces.push_back({.id = 3, .name = "3", .monitorId = 1, .monitorName = "DP-1", .visible = true});

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, stageOptions());

    assert(frame.workspaces.size() == 4);
    assert(frame.workspaces.at(0).workspaceId == 1);
    assert(frame.workspaces.at(1).workspaceId == 2);
    assert(frame.workspaces.at(2).workspaceId == 3);
    assert(frame.workspaces.at(3).workspaceId == 4);
    assert(frame.workspaces.at(3).empty);

    const auto* dockCard = static_cast<const WorkspaceCard*>(nullptr);
    for (const auto& ws : frame.workspaces) {
        if (ws.rect.height == 64.0)
            dockCard = &ws;
    }
    assert(dockCard != nullptr);
    assert(dockCard->workspaceId == 1);
    assert(dockCard->rect.width == 120.0);

    assert(frame.workspaces.at(3).rect.height > 200.0);
}

void focusedStageWindowPaddingMatchesSpec() {
    auto state = sampleState();
    state.windows.push_back({.stableId = 11, .title = "Browser", .workspaceId = 2, .monitorId = 1, .mapped = true});

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, stageOptions());

    const auto& active = frame.workspaces.at(1);
    assert(active.windows.size() == 2);

    const auto& first = active.windows.front();
    assert(first.rect.x == active.rect.x + 16.0);
    assert(first.rect.y == active.rect.y + 44.0);
    assert(first.rect.width == active.rect.width - 32.0);

    const auto totalWindowHeight = active.windows.at(0).rect.height + active.windows.at(1).rect.height;
    const auto totalGap = 8.0;
    const auto expectedHeight = active.rect.height - 44.0 - 16.0 - totalGap;
    assert(std::abs(totalWindowHeight - expectedHeight) < 0.5);
}

void multiMonitorPerFrameBounds() {
    RadiantState state;
    state.monitors.push_back({.id = 1, .name = "DP-1", .geometry = {.size = {.width = 1920, .height = 1080}}, .activeWorkspaceId = 2, .activeWorkspaceName = "2"});
    state.monitors.push_back({.id = 2, .name = "HDMI-A-1", .geometry = {.size = {.width = 1920, .height = 1080}}, .activeWorkspaceId = 5, .activeWorkspaceName = "5"});

    state.workspaces.push_back({.id = 1, .name = "1", .monitorId = 1, .monitorName = "DP-1"});
    state.workspaces.push_back({.id = 2, .name = "2", .monitorId = 1, .monitorName = "DP-1", .visible = true});
    state.workspaces.push_back({.id = 4, .name = "4", .monitorId = 2, .monitorName = "HDMI-A-1"});
    state.workspaces.push_back({.id = 5, .name = "5", .monitorId = 2, .monitorName = "HDMI-A-1", .visible = true});

    const auto frame1 = WorkspaceWallLayout{}.compute(state, state.monitors.at(0), {.width = 1920, .height = 1080}, stageOptions());
    const auto frame2 = WorkspaceWallLayout{}.compute(state, state.monitors.at(1), {.width = 1920, .height = 1080}, stageOptions());

    assert(frame1.monitorId == 1);
    assert(frame1.workspaces.size() == 3);
    assert(frame1.workspaces.at(0).workspaceId == 1);
    assert(frame1.workspaces.at(1).workspaceId == 2);
    assert(frame1.workspaces.at(2).workspaceId == 3);

    assert(frame2.monitorId == 2);
    assert(frame2.workspaces.size() == 3);
    assert(frame2.workspaces.at(0).workspaceId == 4);
    assert(frame2.workspaces.at(1).workspaceId == 5);
    assert(frame2.workspaces.at(2).workspaceId == 6);
    assert(frame2.workspaces.at(2).empty);
}

void searchPanelGeometryMatchesSpec() {
    WorkspaceWallFrame frame;
    frame.bounds = {.x = 0.0, .y = 0.0, .width = 1920.0, .height = 1080.0};

    const auto geom = computeSearchPanelGeometry(frame, 4);

    assert(geom.panelW == 640.0);
    assert(geom.panelH <= 540.0);
    assert(geom.inputH == 48.0);
    assert(geom.inputW == 600.0);
    assert(geom.inputX == geom.panelX + 20.0);
    assert(geom.inputY == geom.panelY + 20.0);
    assert(geom.rowHeight == 56.0);
    assert(geom.rowGap == 6.0);
    assert(geom.resultsY == geom.inputY + geom.inputH + 20.0);
    assert(geom.capacity >= 1);

    const auto centerX = 1920.0 / 2.0;
    const auto centerY = 1080.0 / 2.0;
    assert(std::abs((geom.panelX + geom.panelW / 2.0) - centerX) < 0.5);
    assert(std::abs((geom.panelY + geom.panelH / 2.0) - (centerY - 24.0)) < 0.5);
}

void searchPanelGeometryCapsAtScreenSize() {
    WorkspaceWallFrame frame;
    frame.bounds = {.x = 0.0, .y = 0.0, .width = 600.0, .height = 400.0};

    const auto geom = computeSearchPanelGeometry(frame, 2);

    assert(geom.panelW == 504.0);
    assert(geom.panelH <= 304.0);
    assert(geom.inputW == 464.0);
}

void tinyRenderSizeDoesNotProduceNegativeRects() {
    RadiantState state;
    state.monitors.push_back({.id = 1, .name = "DP-1", .geometry = {.size = {.width = 20, .height = 20}}, .activeWorkspaceId = 1, .activeWorkspaceName = "1"});
    state.workspaces.push_back({.id = 1, .name = "1", .monitorId = 1, .monitorName = "DP-1", .visible = true});
    state.windows.push_back({.stableId = 2, .title = "Two", .workspaceId = 1, .monitorId = 1, .mapped = true});
    state.windows.push_back({.stableId = 1, .title = "One", .workspaceId = 1, .monitorId = 1, .mapped = true});

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 20, .height = 20}, stageOptions());

    for (const auto& workspace : frame.workspaces) {
        assert(workspace.rect.width >= 0.0);
        assert(workspace.rect.height >= 0.0);
        for (const auto& window : workspace.windows) {
            assert(window.rect.width >= 0.0);
            assert(window.rect.height >= 0.0);
        }
    }
}

void sortsWindowsByStableId() {
    RadiantState state;
    state.monitors.push_back({.id = 1, .name = "DP-1", .geometry = {.size = {.width = 1920, .height = 1080}}, .activeWorkspaceId = 1, .activeWorkspaceName = "1"});
    state.workspaces.push_back({.id = 1, .name = "1", .monitorId = 1, .monitorName = "DP-1", .visible = true});
    state.windows.push_back({.stableId = 30, .title = "Thirty", .workspaceId = 1, .monitorId = 1, .mapped = true});
    state.windows.push_back({.stableId = 10, .title = "Ten", .workspaceId = 1, .monitorId = 1, .mapped = true});

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, stageOptions());

    assert(frame.workspaces.front().windows.size() == 2);
    assert(frame.workspaces.front().windows.at(0).stableId == 10);
    assert(frame.workspaces.front().windows.at(1).stableId == 30);
}

void fillsEmptySlotsAndFallsBackToClassName() {
    RadiantState state;
    state.monitors.push_back({.id = 7, .name = "HDMI-A-1", .geometry = {.size = {.width = 1280, .height = 720}}, .activeWorkspaceId = 4, .activeWorkspaceName = "4"});
    state.workspaces.push_back({.id = 4, .name = "4", .monitorId = 7, .monitorName = "HDMI-A-1", .visible = true});
    state.windows.push_back({.stableId = 44, .className = "Firefox", .workspaceId = 4, .monitorId = 7, .mapped = true});

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1280, .height = 720});

    assert(frame.workspaces.size() == 6);
    assert(frame.workspaces.at(0).workspaceId == 1);
    assert(frame.workspaces.at(3).workspaceId == 4);
    assert(frame.workspaces.at(3).windows.size() == 1);
    assert(frame.workspaces.at(3).windows.front().label == "Firefox");
    assert(frame.workspaces.at(5).empty);
}

} // namespace

int main() {
    gridLayoutFillsMinimumSlots();
    polishedDefaultsLeaveBreathingRoom();
    focusedStageUsesRealWorkspacesPlusEmptyNext();
    focusedStageDoesNotGeneratePhantomWorkspacesForGaps();
    focusedStageCardSpacingMatchesSpec();
    focusedStageEmptyNextWorkspaceGeometry();
    focusedStageWindowPaddingMatchesSpec();
    multiMonitorPerFrameBounds();
    searchPanelGeometryMatchesSpec();
    searchPanelGeometryCapsAtScreenSize();
    tinyRenderSizeDoesNotProduceNegativeRects();
    sortsWindowsByStableId();
    fillsEmptySlotsAndFallsBackToClassName();
    std::cout << "WorkspaceWallLayoutTest passed\n";
    return 0;
}

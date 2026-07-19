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
    state.windows.push_back({
        .stableId = 10,
        .title = "Editor",
        .className = "Code",
        .geometry = {.position = {.x = 120, .y = 90}, .size = {.width = 900, .height = 760}},
        .workspaceId = 2,
        .monitorId = 1,
        .mapped = true,
    });
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
    assert(frame.workspaces.at(2).createTarget);
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
    assert(frame.workspaces.at(2).createTarget);
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

    const auto cardH = std::clamp(1080.0 * 0.145, 96.0, 168.0);
    const auto cardW = cardH * 1920.0 / 1080.0;
    const auto gap   = std::clamp(1920.0 * 0.00625, 8.0, 14.0);

    assert(frame.focusedStage);
    assert(active.rect.width == cardW);
    assert(active.rect.height == cardH);
    assert(std::abs(active.rect.x + active.rect.width / 2.0 - 960.0) < 0.5);
    assert(left.rect.width == cardW);
    assert(right.rect.width == cardW);
    assert(std::abs((active.rect.x - left.rect.x - left.rect.width) - gap) < 0.5);
    assert(std::abs((right.rect.x - active.rect.x - active.rect.width) - gap) < 0.5);
    assert(frame.rail.bounds.y < active.rect.y);
    assert(frame.stage.bounds.y > frame.rail.bounds.y + frame.rail.bounds.height);
    assert(frame.stage.workspaceId == 2);
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

    for (const auto& workspace : frame.workspaces) {
        assert(workspace.rect.width == frame.workspaces.front().rect.width);
        assert(workspace.rect.height == frame.workspaces.front().rect.height);
    }
    assert(frame.stage.workspaceId == 3);
}

void focusedStageMapsWindowGeometrySpatially() {
    auto state = sampleState();
    state.windows.push_back({
        .stableId = 11,
        .title = "Browser",
        .geometry = {.position = {.x = 1100, .y = 180}, .size = {.width = 700, .height = 620}},
        .workspaceId = 2,
        .monitorId = 1,
        .mapped = true,
    });

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, stageOptions());

    const auto& active = frame.workspaces.at(1);
    assert(active.windows.size() == 2);
    assert(frame.stage.windows.size() == 2);
    assert(frame.stage.windows.at(0).stableId == 10);
    assert(frame.stage.windows.at(1).stableId == 11);
    assert(frame.stage.windows.at(0).rect.x < frame.stage.windows.at(1).rect.x);

    const auto scale = std::min(frame.stage.bounds.width / 1920.0, frame.stage.bounds.height / 1080.0);
    assert(std::abs(frame.stage.windows.at(0).rect.width - 900.0 * scale) < 0.5);
    assert(std::abs(frame.stage.windows.at(1).rect.height - 620.0 * scale) < 0.5);
}

void focusedStageCentersOverflowAroundPreview() {
    RadiantState state;
    state.monitors.push_back({.id = 1, .name = "DP-1", .geometry = {.size = {.width = 1280, .height = 720}}, .activeWorkspaceId = 1});
    for (std::int64_t id = 1; id <= 9; ++id)
        state.workspaces.push_back({.id = id, .name = std::to_string(id), .monitorId = 1});

    auto options = stageOptions();
    options.previewWorkspaceId = 6;
    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1280, .height = 720}, options);

    assert(frame.rail.overflowLeft);
    assert(frame.rail.overflowRight);
    assert(frame.stage.workspaceId == 6);
    const auto& selected = frame.workspaces.at(5);
    assert(std::abs(selected.rect.x + selected.rect.width / 2.0 - 640.0) < 0.5);
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

    assert(geom.panelW == 680.0);
    assert(geom.panelH == 370.0);
    assert(geom.inputH == 48.0);
    assert(geom.inputW == 640.0);
    assert(geom.inputX == geom.panelX + 20.0);
    assert(geom.inputY == geom.panelY + 20.0);
    assert(geom.rowHeight == 56.0);
    assert(geom.rowGap == 6.0);
    assert(geom.resultsY == geom.inputY + geom.inputH + 20.0);
    assert(geom.capacity == 4);

    const auto centerX = 1920.0 / 2.0;
    assert(std::abs((geom.panelX + geom.panelW / 2.0) - centerX) < 0.5);
    assert(std::abs((geom.panelY + geom.panelH / 2.0) - (1080.0 * 0.35)) < 0.5);
}

void searchPanelGeometryCapsAtScreenSize() {
    WorkspaceWallFrame frame;
    frame.bounds = {.x = 0.0, .y = 0.0, .width = 600.0, .height = 400.0};

    const auto geom = computeSearchPanelGeometry(frame, 2);

    assert(geom.panelW == 552.0);
    assert(geom.panelH <= 304.0);
    assert(geom.inputW == 512.0);
    assert(geom.panelX >= 24.0);
    assert(geom.panelY >= 48.0);
    assert(geom.panelX + geom.panelW <= 576.0);
    assert(geom.panelY + geom.panelH <= 352.0);
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

void groupedModeOrdersApplicationsAndMarksHeaders() {
    auto state = sampleState();
    state.windows.push_back({.stableId = 11, .title = "Browser", .className = "Firefox", .workspaceId = 2, .monitorId = 1, .mapped = true});
    state.windows.push_back({.stableId = 12, .title = "Second editor", .className = "Code", .workspaceId = 2, .monitorId = 1, .mapped = true});
    auto options = stageOptions();
    options.mode = OverviewMode::Grouped;

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, options);

    assert(frame.stage.windows.size() == 3);
    assert(frame.stage.windows.at(0).appClass == "Code");
    assert(frame.stage.windows.at(0).appGroupStart);
    assert(!frame.stage.windows.at(1).appGroupStart);
    assert(frame.stage.windows.at(2).appClass == "Firefox");
    assert(frame.stage.windows.at(2).appGroupStart);
}

void appExposeFiltersAcrossLocalWorkspaces() {
    auto state = sampleState();
    state.windows.push_back({.stableId = 20, .title = "Other space", .className = "Code", .workspaceId = 1, .monitorId = 1, .mapped = true});
    state.windows.push_back({.stableId = 21, .title = "Other app", .className = "Firefox", .workspaceId = 1, .monitorId = 1, .mapped = true});
    state.windows.push_back({.stableId = 22, .title = "Other monitor", .className = "Code", .workspaceId = 3, .monitorId = 2, .mapped = true});
    auto options = stageOptions();
    options.mode = OverviewMode::AppExpose;
    options.applicationFilter = "Code";

    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080}, options);

    assert(frame.stage.name == "Code");
    assert(frame.stage.windows.size() == 2);
    assert(frame.stage.windows.at(0).stableId == 20);
    assert(frame.stage.windows.at(1).stableId == 10);
}

} // namespace

int main() {
    gridLayoutFillsMinimumSlots();
    polishedDefaultsLeaveBreathingRoom();
    focusedStageUsesRealWorkspacesPlusEmptyNext();
    focusedStageDoesNotGeneratePhantomWorkspacesForGaps();
    focusedStageCardSpacingMatchesSpec();
    focusedStageEmptyNextWorkspaceGeometry();
    focusedStageMapsWindowGeometrySpatially();
    focusedStageCentersOverflowAroundPreview();
    multiMonitorPerFrameBounds();
    searchPanelGeometryMatchesSpec();
    searchPanelGeometryCapsAtScreenSize();
    tinyRenderSizeDoesNotProduceNegativeRects();
    sortsWindowsByStableId();
    fillsEmptySlotsAndFallsBackToClassName();
    groupedModeOrdersApplicationsAndMarksHeaders();
    appExposeFiltersAcrossLocalWorkspaces();
    std::cout << "WorkspaceWallLayoutTest passed\n";
    return 0;
}

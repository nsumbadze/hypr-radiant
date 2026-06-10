#include <hypr-radiant/WorkspaceWallLayout.hpp>

#include <cassert>
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

void computesMinimumWorkspaceSlots() {
    const auto state = sampleState();
    const auto frame = WorkspaceWallLayout{}.compute(state, state.monitors.front(), {.width = 1920, .height = 1080});

    assert(frame.monitorId == 1);
    assert(frame.workspaces.size() == 6);
    assert(frame.workspaces.at(1).workspaceId == 2);
    assert(frame.workspaces.at(1).active);
    assert(frame.workspaces.at(1).windows.size() == 1);
    assert(frame.workspaces.at(1).windows.front().label == "Editor");
}

} // namespace

int main() {
    computesMinimumWorkspaceSlots();
    std::cout << "WorkspaceWallLayoutTest passed\n";
    return 0;
}

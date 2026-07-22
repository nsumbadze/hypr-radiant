#include <hypr-radiant/StateCollector.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

#include <cstdint>

namespace hypr_radiant {
namespace {

RadiantGeometry geometryFrom(const Vector2D& position, const Vector2D& size) {
    return {
        .position = {.x = position.x, .y = position.y},
        .size     = {.width = size.x, .height = size.y},
    };
}

std::int64_t monitorId(const PHLMONITOR& monitor) {
    if (!monitor)
        return -1;

    return static_cast<std::int64_t>(monitor->m_id);
}

std::int64_t workspaceId(const PHLWORKSPACE& workspace) {
    if (!valid(workspace))
        return -1;

    return static_cast<std::int64_t>(workspace->m_id);
}

std::string workspaceName(const PHLWORKSPACE& workspace) {
    if (!valid(workspace))
        return "";

    return workspace->m_name;
}

} // namespace

RadiantState StateCollector::collect() const {
    RadiantState state;

    if (!g_pCompositor) {
        log::warn("cannot collect state: Hyprland compositor is not available");
        return state;
    }

    for (const auto& monitor : g_pCompositor->m_monitors) {
        if (!monitor || !g_pCompositor->monitorExists(monitor))
            continue;

        const auto activeWorkspace = monitor->m_activeWorkspace;

        state.monitors.push_back({
            .id                  = monitorId(monitor),
            .name                = monitor->m_name,
            .geometry            = geometryFrom(monitor->m_position, monitor->m_size),
            .activeWorkspaceId   = workspaceId(activeWorkspace),
            .activeWorkspaceName = valid(activeWorkspace) ? activeWorkspace->m_name : "",
        });
    }

    for (const auto& workspace : g_pCompositor->getWorkspacesCopy()) {
        if (!workspace || workspace->inert())
            continue;

        const auto monitor      = workspace->m_monitor.lock();
        const auto validMonitor = monitor && g_pCompositor->monitorExists(monitor);

        state.workspaces.push_back({
            .id             = workspaceId(workspace),
            .name           = workspaceName(workspace),
            .monitorId      = validMonitor ? monitorId(monitor) : -1,
            .special        = workspace->m_isSpecialWorkspace,
        });
    }

    for (const auto& window : g_pCompositor->m_windows) {
        if (!window)
            continue;

        const auto workspace    = window->m_workspace;
        const auto monitor      = window->m_monitor.lock();
        const auto validMonitor = monitor && g_pCompositor->monitorExists(monitor);

        state.windows.push_back({
            .stableId      = window->m_stableID,
            .title         = window->m_title,
            .className     = window->m_class,
            .geometry      = geometryFrom(window->m_realPosition->value(), window->m_realSize->value()),
            .workspaceId   = workspaceId(workspace),
            .monitorId     = validMonitor ? monitorId(monitor) : -1,
            .mapped        = window->m_isMapped,
            .floating      = window->m_isFloating,
            .fullscreen    = window->m_fullscreenState.internal != FSMODE_NONE || window->m_fullscreenState.client != FSMODE_NONE,
        });
    }

    return state;
}

} // namespace hypr_radiant

#include <hypr-radiant/compositor/StateCollector.hpp>
#include <hypr-radiant/HyprlandCompat.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>

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

    for (const auto& monitor : HyprlandCompat::monitors()) {
        if (!HyprlandCompat::monitorExists(monitor))
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

    for (const auto& workspace : HyprlandCompat::workspaces()) {
        if (!workspace || workspace->inert())
            continue;

        const auto monitor      = workspace->m_monitor.lock();
        const auto validMonitor = HyprlandCompat::monitorExists(monitor);

        state.workspaces.push_back({
            .id             = workspaceId(workspace),
            .name           = workspaceName(workspace),
            .monitorId      = validMonitor ? monitorId(monitor) : -1,
            .special        = workspace->m_isSpecialWorkspace,
        });
    }

    for (const auto& window : HyprlandCompat::windows()) {
        if (!window)
            continue;

        const auto workspace    = window->m_workspace;
        const auto monitor      = window->m_monitor.lock();
        const auto validMonitor = HyprlandCompat::monitorExists(monitor);

        state.windows.push_back({
            .stableId      = window->m_stableID,
            .title         = window->m_title,
            .className     = window->m_class,
            .geometry      = geometryFrom(HyprlandCompat::windowPosition(window), HyprlandCompat::windowSize(window)),
            .workspaceId   = workspaceId(workspace),
            .monitorId     = validMonitor ? monitorId(monitor) : -1,
            .mapped        = window->m_isMapped,
            .floating      = window->m_isFloating,
            .fullscreen    = HyprlandCompat::windowFullscreen(window),
        });
    }

    return state;
}

} // namespace hypr_radiant

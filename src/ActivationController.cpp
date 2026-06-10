#include <hypr-radiant/ActivationController.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

namespace hypr_radiant {
namespace {

PHLWINDOW windowByStableId(std::uint64_t stableId) {
    if (!g_pCompositor)
        return nullptr;

    for (const auto& window : g_pCompositor->m_windows) {
        if (window && window->m_stableID == stableId)
            return window;
    }

    return nullptr;
}

bool activateWorkspace(std::int64_t workspaceId) {
    if (!g_pCompositor)
        return false;

    const auto workspace = g_pCompositor->getWorkspaceByID(static_cast<WORKSPACEID>(workspaceId));
    if (!workspace)
        return false;

    const auto monitor = workspace->m_monitor.lock();
    if (!monitor || !g_pCompositor->monitorExists(monitor))
        return false;

    monitor->changeWorkspace(workspace, false, false, false);
    return true;
}

} // namespace

bool ActivationController::activate(const OverviewTarget& target) const {
    if (target.type == OverviewTargetType::Workspace)
        return activateWorkspace(target.workspaceId);

    if (target.type != OverviewTargetType::Window)
        return false;

    const auto window = windowByStableId(target.windowId);
    if (!window || !window->m_workspace)
        return false;

    if (!activateWorkspace(window->workspaceID()))
        return false;

    Desktop::focusState()->fullWindowFocus(window, Desktop::FOCUS_REASON_SWITCH_TO_WINDOW_HARD);
    return true;
}

} // namespace hypr_radiant

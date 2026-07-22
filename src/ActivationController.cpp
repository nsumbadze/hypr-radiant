#include <hypr-radiant/ActivationController.hpp>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/XWaylandManager.hpp>

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

bool activateWorkspace(std::int64_t workspaceId, std::int64_t monitorId = -1) {
    if (!g_pCompositor)
        return false;

    auto workspace = g_pCompositor->getWorkspaceByID(static_cast<WORKSPACEID>(workspaceId));
    if (!workspace && monitorId >= 0)
        workspace = g_pCompositor->createNewWorkspace(static_cast<WORKSPACEID>(workspaceId), static_cast<MONITORID>(monitorId));
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
    if (target.type == OverviewTargetType::Workspace || target.type == OverviewTargetType::NewWorkspace)
        return activateWorkspace(target.workspaceId, target.monitorId);

    if (target.type != OverviewTargetType::Window)
        return false;

    const auto window = windowByStableId(target.windowId);
    if (!window || !window->m_workspace)
        return false;

    if (!activateWorkspace(window->workspaceID()))
        return false;

    const auto focus = Desktop::focusState();
    if (!focus)
        return false;

    focus->fullWindowFocus(window, Desktop::FOCUS_REASON_SWITCH_TO_WINDOW_HARD);
    return true;
}

bool ActivationController::moveWindow(std::uint64_t windowId, std::int64_t workspaceId, std::int64_t monitorId) const {
    if (!g_pCompositor || workspaceId <= 0 || monitorId < 0)
        return false;

    const auto window = windowByStableId(windowId);
    if (!window)
        return false;

    auto workspace = g_pCompositor->getWorkspaceByID(static_cast<WORKSPACEID>(workspaceId));
    if (!workspace)
        workspace = g_pCompositor->createNewWorkspace(static_cast<WORKSPACEID>(workspaceId), static_cast<MONITORID>(monitorId));
    if (!workspace)
        return false;

    g_pCompositor->moveWindowToWorkspaceSafe(window, workspace);
    return true;
}

bool ActivationController::closeWindow(std::uint64_t windowId) const {
    if (!g_pXWaylandManager)
        return false;

    const auto window = windowByStableId(windowId);
    if (!window)
        return false;

    // A polite close, the same request a titlebar close button sends: the client still gets to
    // prompt about unsaved work, and may refuse outright.
    g_pXWaylandManager->sendCloseWindow(window);
    return true;
}

} // namespace hypr_radiant

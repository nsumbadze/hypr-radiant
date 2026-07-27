#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>

#if __has_include(<hyprland/src/state/MonitorState.hpp>)
#define HYPR_RADIANT_HYPRLAND_STATE_API 1
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#else
#define HYPR_RADIANT_HYPRLAND_STATE_API 0
#include <hyprland/src/helpers/Monitor.hpp>
#endif

#include <functional>
#include <utility>
#include <vector>

namespace hypr_radiant::HyprlandCompat {

inline const std::vector<PHLWINDOW>& windows() {
    static const std::vector<PHLWINDOW> empty;

#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& state = Desktop::windowState();
    return state ? state->windows() : empty;
#else
    return g_pCompositor ? g_pCompositor->m_windows : empty;
#endif
}

inline Vector2D windowPosition(const PHLWINDOW& window) {
    if (!window)
        return {};

#if HYPR_RADIANT_HYPRLAND_STATE_API
    return window->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
#else
    return window->m_realPosition->value();
#endif
}

inline Vector2D windowSize(const PHLWINDOW& window) {
    if (!window)
        return {};

#if HYPR_RADIANT_HYPRLAND_STATE_API
    return window->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
#else
    return window->m_realSize->value();
#endif
}

inline bool windowFullscreen(const PHLWINDOW& window) {
    if (!window)
        return false;

#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& controller = Fullscreen::controller();
    return controller && controller->isFullscreen(window);
#else
    return window->m_fullscreenState.internal != FSMODE_NONE || window->m_fullscreenState.client != FSMODE_NONE;
#endif
}

template <typename Callback>
inline CHyprSignalListener listenWindowDestroy(Callback callback) {
#if HYPR_RADIANT_HYPRLAND_STATE_API
    return Event::bus()->m_events.window.destroy.listen(
        std::function<void(const PHLWINDOWREF&)>{[callback = std::move(callback)](const PHLWINDOWREF& window) mutable {
            callback(window.lock());
        }});
#else
    return Event::bus()->m_events.window.destroy.listen(
        std::function<void(PHLWINDOW)>{[callback = std::move(callback)](PHLWINDOW window) mutable {
            callback(std::move(window));
        }});
#endif
}

inline const std::vector<PHLMONITOR>& monitors() {
    static const std::vector<PHLMONITOR> empty;

#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& state = State::monitorState();
    return state ? state->monitors() : empty;
#else
    return g_pCompositor ? g_pCompositor->m_monitors : empty;
#endif
}

inline std::vector<PHLWORKSPACE> workspaces() {
#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& state = State::workspaceState();
    return state ? state->workspacesCopy() : std::vector<PHLWORKSPACE>{};
#else
    return g_pCompositor ? g_pCompositor->getWorkspacesCopy() : std::vector<PHLWORKSPACE>{};
#endif
}

inline bool monitorExists(const PHLMONITOR& monitor) {
    if (!monitor)
        return false;

#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& state = State::monitorState();
    return state && state->contains(monitor);
#else
    return g_pCompositor && g_pCompositor->monitorExists(monitor);
#endif
}

inline PHLWORKSPACE workspaceById(WORKSPACEID id) {
#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& state = State::workspaceState();
    return state ? state->query().id(id).run() : nullptr;
#else
    return g_pCompositor ? g_pCompositor->getWorkspaceByID(id) : nullptr;
#endif
}

inline PHLWORKSPACE createWorkspace(WORKSPACEID id, MONITORID monitorId) {
#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& state = State::workspaceState();
    return state ? state->create(id, monitorId) : nullptr;
#else
    return g_pCompositor ? g_pCompositor->createNewWorkspace(id, monitorId) : nullptr;
#endif
}

inline bool moveWindowToWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
    if (!window || !workspace)
        return false;

#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& controller = Desktop::globalWindowController();
    if (!controller)
        return false;
    controller->moveWindowToWorkspace(window, workspace);
#else
    if (!g_pCompositor)
        return false;
    g_pCompositor->moveWindowToWorkspaceSafe(window, workspace);
#endif

    return true;
}

inline PHLMONITOR monitorFromCursor() {
#if HYPR_RADIANT_HYPRLAND_STATE_API
    const auto& state = State::monitorState();
    if (!state || !g_pInputManager)
        return nullptr;
    return state->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
#else
    return g_pCompositor ? g_pCompositor->getMonitorFromCursor() : nullptr;
#endif
}

inline void scheduleFrame(const PHLMONITOR& monitor) {
    if (!monitor)
        return;

#if HYPR_RADIANT_HYPRLAND_STATE_API
    monitor->scheduleFrame();
#else
    if (g_pCompositor)
        g_pCompositor->scheduleFrameForMonitor(monitor);
#endif
}

} // namespace hypr_radiant::HyprlandCompat

#undef HYPR_RADIANT_HYPRLAND_STATE_API

#include <hypr-radiant/RadiantPlugin.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr auto PLUGIN_NAME        = "hypr-radiant";
constexpr auto DISPATCHER_TOGGLE  = "radiant:toggle";
constexpr auto DISPATCHER_OPEN    = "radiant:open";
constexpr auto DISPATCHER_CLOSE   = "radiant:close";
constexpr auto DISPATCHER_APP     = "radiant:app";
constexpr auto DISPATCHER_SHELF   = "radiant:shelf";
constexpr auto DISPATCHER_STATUS  = "radiant:status";
constexpr auto PLUGIN_DESCRIPTION = "Native workspace overview with live previews and search.";
constexpr auto PLUGIN_AUTHOR      = "Nika Sumbadze (@nsumbadze)";
constexpr auto PLUGIN_VERSION     = "0.3.0-dev";
constexpr auto TOGGLE_GUARD_DELAY = std::chrono::milliseconds{300};
constexpr auto POST_DROP_ACTIVATION_DELAY = std::chrono::milliseconds{320};
constexpr auto CLOSE_RESPONSE_DELAY = std::chrono::milliseconds{140};

HANDLE g_pluginHandle = nullptr;
std::unique_ptr<hypr_radiant::RadiantPlugin> g_plugin;

void resetPluginState() {
    if (g_plugin)
        g_plugin->shutdown();

    g_plugin.reset();
    g_pluginHandle = nullptr;
}

void assertCompatibleHeaders() {
    const std::string compositorHash = __hyprland_api_get_hash();
    const std::string pluginHash     = __hyprland_api_get_client_hash();

    if (compositorHash == pluginHash)
        return;

    HyprlandAPI::addNotification(
        g_pluginHandle,
        "[hypr-radiant] mismatched Hyprland headers; refusing to load",
        CHyprColor{1.0F, 0.2F, 0.2F, 1.0F},
        5000);

    hypr_radiant::log::error("mismatched headers: compositor={}, plugin={}", compositorHash, pluginHash);
    resetPluginState();
    throw std::runtime_error{"hypr-radiant: mismatched Hyprland headers"};
}

} // namespace

namespace hypr_radiant {

RadiantPlugin::RadiantPlugin(HANDLE handle) : m_handle(handle), m_overlay(m_config) {}

bool RadiantPlugin::initialize() {
    if (!m_config.registerValues(m_handle)) {
        log::error("failed to register config values");
        return false;
    }

    m_shortcut.install([this] { return m_config.shortcutEnabled(); });
    m_overlay.install();
    // Re-collect whenever a window goes away while the overview is up. Without this the card for a
    // closed window lingers as an empty surface, whether it was closed from the overview's own
    // button or from a keybind behind it.
    // Only destroy, not close: close fires on request and destroy on the window actually going
    // away, so listening to both re-collected and re-rasterised every label twice per closed window.
    // A window that refuses to close never destroys, and correctly never triggers a re-layout.
    m_windowDestroyListener = Event::bus()->m_events.window.destroy.listen([this](PHLWINDOW window) {
        if (window && window->m_stableID == m_pendingCloseWindowId)
            cancelPendingWindowClose(false);
        if (m_overlay.active())
            m_overlay.refresh(m_stateCollector.collect());
    });
    m_input.install({
        .active   = [this] { return m_overlay.active(); },
        .activate = [this](OverviewTarget target) { activate(target, "keyboard activation"); },
        .pointerMove = [this](double x, double y) { m_overlay.pointerMoved(x, y); },
        .pointerButton = [this](bool pressed, double x, double y) {
            const auto action = m_overlay.pointerButton(pressed, x, y);
            if (action.type == PointerActionType::Activate) {
                activate(action.target, "pointer activation");
                return;
            }
            if (action.type == PointerActionType::CloseWindow) {
                beginWindowClose(action.windowId);
                return;
            }
            if (action.type != PointerActionType::MoveWindow && action.type != PointerActionType::CreateWorkspaceAndMoveWindow)
                return;
            if (!m_activation.moveWindow(action.windowId, action.target.workspaceId, action.target.monitorId)) {
                log::warn("window move target disappeared or was invalid");
                return;
            }
            if (action.type == PointerActionType::CreateWorkspaceAndMoveWindow)
                log::info("created workspace {} and moved window {}", action.target.workspaceId, action.windowId);
            m_input.deferActivation(POST_DROP_ACTIVATION_DELAY);
            m_overlay.refresh(m_stateCollector.collect()); },
        .textInput = [this](char value) { m_overlay.appendSearchChar(value); },
        .backspace = [this] { m_overlay.backspaceSearch(); },
        .move = [this](NavigationDirection direction) { m_overlay.moveSelection(direction); },
        // Scroll stays bound to the workspace shelf alone. The hint dock is pointer-only: scrolling
        // down is already the close gesture, so revealing the dock on it fought that.
        .shelfScroll = [this](bool reveal) { m_overlay.setWorkspaceShelfVisible(reveal); },
        .searchActive = [this] { return m_overlay.searchActive(); },
        .openSearch = [this] { m_overlay.beginSearch(); },
        .jump = [this](std::int64_t workspaceId) { activate({.type = OverviewTargetType::Workspace, .workspaceId = workspaceId}, "number activation"); },
        .close = [this] {
            const auto wasActive = m_overlay.active();
            m_overlay.clearSearchOrHide();
            if (wasActive && !m_overlay.active()) {
                recordTransition("closed by Escape");
                m_input.releaseKeyboard();
            } },
        .toggleMode = [this] { m_overlay.toggleGroupedMode(); },
    });
    m_gestures.install({
        .enabled = [this] { return m_config.gestureEnabled(); },
        .fingers = [this] { return m_config.gestureFingers(); },
        .distance = [this] { return m_config.gestureDistance(); },
        .overviewActive = [this] { return m_overlay.active(); },
        .shelfVisible = [this] { return m_overlay.workspaceShelfVisible(); },
        .begin = [this](SwipeAction action) {
            if (action == SwipeAction::OpenOverview) {
                m_config.refreshPalette();
                m_overlay.beginGestureOpen(m_stateCollector.collect());
                m_lastOpenedAt = Clock::now();
                m_input.grabKeyboard(InputController::OpeningRelease::Skip);
            } },
        .update = [this](SwipeAction action, double progress) {
            if (action == SwipeAction::OpenOverview || action == SwipeAction::CloseOverview)
                m_overlay.setGestureProgress(action == SwipeAction::OpenOverview, progress);
            else if (action == SwipeAction::RevealShelf || action == SwipeAction::HideShelf)
                m_overlay.setWorkspaceShelfGestureProgress(action == SwipeAction::RevealShelf, progress); },
        .end = [this](SwipeAction action, bool commit) {
            if (action == SwipeAction::RevealShelf || action == SwipeAction::HideShelf) {
                m_overlay.finishWorkspaceShelfGesture(action == SwipeAction::RevealShelf, commit);
                return;
            }
            if (action == SwipeAction::PreviousWorkspace || action == SwipeAction::NextWorkspace) {
                if (commit)
                    m_overlay.moveSelection(action == SwipeAction::PreviousWorkspace ? NavigationDirection::Left : NavigationDirection::Right);
                return;
            }
            const auto opening = action == SwipeAction::OpenOverview;
            m_overlay.finishGesture(opening, commit);
            const auto remainsVisible = opening ? commit : !commit;
            if (remainsVisible)
                m_input.grabKeyboard(InputController::OpeningRelease::Skip);
            else {
                recordTransition(opening ? "opening gesture cancelled" : "closed by gesture");
                m_input.releaseKeyboard();
            } },
    });
    return true;
}

SDispatchResult RadiantPlugin::showApplication(const std::string& args) {
    if (!args.empty())
        log::warn("radiant:app ignores dispatcher arguments: {}", args);

    const auto focused = Desktop::focusState() ? Desktop::focusState()->window() : nullptr;
    if (!focused || focused->m_class.empty())
        return {.passEvent = false, .success = false, .error = "no focused application"};

    auto state = m_stateCollector.collect();
    m_config.refreshPalette();
    m_overlay.showAppExpose(std::move(state), focused->m_class);
    recordTransition(std::format("opened App Expose for {}", focused->m_class));
    m_lastOpenedAt = Clock::now();
    m_input.grabKeyboard();
    log::info("app expose opened for {}", focused->m_class);
    return {.passEvent = false, .success = true, .error = ""};
}

void RadiantPlugin::shutdown() {
    cancelPendingWindowClose(false);
    m_windowDestroyListener.reset();
    m_gestures.uninstall();
    m_input.uninstall();
    m_overlay.uninstall();
    m_shortcut.uninstall();
}

void RadiantPlugin::beginWindowClose(std::uint64_t windowId) {
    if (m_pendingCloseWindowId != 0)
        return;

    const auto duration = m_overlay.beginWindowClose(windowId);
    if (!duration)
        return;

    m_input.deferActivation(*duration + CLOSE_RESPONSE_DELAY);

    if (duration->count() == 0 || !g_pEventLoopManager) {
        if (!m_activation.closeWindow(windowId))
            log::warn("close target disappeared before the request was sent");
        m_overlay.completeWindowClose(windowId);
        return;
    }

    m_pendingCloseWindowId = windowId;
    m_closeRequestSent     = false;
    m_windowCloseTimer     = makeShared<CEventLoopTimer>(
        *duration,
        [this, windowId](SP<CEventLoopTimer> self, void*) {
            if (windowId != m_pendingCloseWindowId)
                return;

            if (!m_closeRequestSent) {
                m_closeRequestSent = true;
                if (!m_activation.closeWindow(windowId)) {
                    log::warn("close target disappeared before the request was sent");
                    cancelPendingWindowClose(true);
                    return;
                }

                // Most clients disappear before this fires. If one refuses the polite close or
                // opens a save prompt, bring its card back instead of leaving a permanent hole.
                self->updateTimeout(CLOSE_RESPONSE_DELAY);
                return;
            }

            cancelPendingWindowClose(true);
        },
        nullptr);
    g_pEventLoopManager->addTimer(m_windowCloseTimer);
}

void RadiantPlugin::cancelPendingWindowClose(bool restoreCard) {
    const auto windowId = m_pendingCloseWindowId;
    if (m_windowCloseTimer) {
        m_windowCloseTimer->cancel();
        if (g_pEventLoopManager)
            g_pEventLoopManager->removeTimer(m_windowCloseTimer);
        m_windowCloseTimer.reset();
    }

    m_pendingCloseWindowId = 0;
    m_closeRequestSent     = false;

    if (windowId == 0)
        return;
    if (restoreCard)
        m_overlay.cancelWindowClose(windowId);
    else
        m_overlay.completeWindowClose(windowId);
}

void RadiantPlugin::activate(OverviewTarget target, std::string_view source) {
    if (target.type == OverviewTargetType::None)
        target = m_overlay.selectedTarget();

    if (target.type == OverviewTargetType::None)
        return;

    if (!m_activation.activate(target)) {
        log::warn("overview activation target disappeared or was invalid");
        return;
    }

    m_input.releaseKeyboard();
    m_overlay.hideImmediate();
    recordTransition(std::format("closed by {}", source));
}

void RadiantPlugin::recordTransition(std::string message) {
    m_lastTransition = std::move(message);
    log::info("{}", m_lastTransition);
}

SDispatchResult RadiantPlugin::status(const std::string&) {
    const auto message = std::format("active={} | {}", m_overlay.active() ? "true" : "false", m_lastTransition);
    HyprlandAPI::addNotification(m_handle, std::format("[hypr-radiant] {}", message), CHyprColor{0.31F, 0.58F, 0.46F, 1.0F}, 5000);
    return {.passEvent = false, .success = true, .error = ""};
}

SDispatchResult RadiantPlugin::shelf(const std::string& args) {
    if (!m_overlay.active())
        return {.passEvent = false, .success = false, .error = "overview is not active"};

    if (args.empty() || args == "toggle")
        m_overlay.toggleWorkspaceShelf();
    else if (args == "show" || args == "open")
        m_overlay.setWorkspaceShelfVisible(true);
    else if (args == "hide" || args == "close")
        m_overlay.setWorkspaceShelfVisible(false);
    else
        return {.passEvent = false, .success = false, .error = "expected show, hide, or toggle"};

    return {.passEvent = false, .success = true, .error = ""};
}

SDispatchResult RadiantPlugin::toggle(const std::string& args) {
    if (!args.empty())
        log::warn("radiant:toggle ignores dispatcher arguments: {}", args);

    const auto now       = Clock::now();
    const bool wasActive = m_overlay.active();
    if (wasActive && now - m_lastOpenedAt < TOGGLE_GUARD_DELAY) {
        log::info("ignored duplicate toggle while overview input is arming");
        return {.passEvent = false, .success = true, .error = ""};
    }

    auto state = m_stateCollector.collect();

    log::info(
        "state snapshot: {} monitors, {} workspaces, {} windows ({} mapped)",
        state.monitors.size(),
        state.workspaces.size(),
        state.windows.size(),
        state.mappedWindowCount());

    m_config.refreshPalette();
    m_overlay.toggle(std::move(state));

    if (!wasActive && m_overlay.active()) {
        recordTransition("opened by dispatcher");
        m_lastOpenedAt = now;
        m_input.grabKeyboard();
    } else if (wasActive && !m_overlay.active()) {
        recordTransition("closed by dispatcher toggle");
        m_input.releaseKeyboard();
    }

    log::info("radiant overlay {}", m_overlay.active() ? "opened" : "closed");

    return {.passEvent = false, .success = true, .error = ""};
}

SDispatchResult RadiantPlugin::open(const std::string& args) {
    if (!args.empty())
        log::warn("radiant:open ignores dispatcher arguments: {}", args);
    if (m_overlay.active())
        return {.passEvent = false, .success = true, .error = ""};

    m_config.refreshPalette();
    m_overlay.show(m_stateCollector.collect());
    m_lastOpenedAt = Clock::now();
    m_input.grabKeyboard();
    recordTransition("opened by explicit dispatcher");
    return {.passEvent = false, .success = true, .error = ""};
}

SDispatchResult RadiantPlugin::close(const std::string& args) {
    if (!args.empty())
        log::warn("radiant:close ignores dispatcher arguments: {}", args);
    if (!m_overlay.active()) {
        // Still release: this dispatcher is the way out if the grab ever outlives the overlay, and
        // returning early would leave the keyboard captured with no overlay left to close.
        m_input.releaseKeyboard();
        return {.passEvent = false, .success = true, .error = ""};
    }

    m_config.refreshPalette();
    m_overlay.toggle(m_stateCollector.collect());
    m_input.releaseKeyboard();
    recordTransition("closed by explicit dispatcher");
    return {.passEvent = false, .success = true, .error = ""};
}

} // namespace hypr_radiant

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    // Hyprland calls pluginInit synchronously after loading the shared object.
    // Keep initialization lightweight and register all Hyprland-facing handles here.
    g_pluginHandle = handle;
    assertCompatibleHeaders();

    g_plugin = std::make_unique<hypr_radiant::RadiantPlugin>(g_pluginHandle);

    // On partial-init failure, resetPluginState()/shutdown() removes hypr-radiant's
    // listeners before throwing; Hyprland 0.55.2 catches the exception and ejects
    // the plugin, removing any API registrations made before the failure.
    if (!g_plugin->initialize()) {
        resetPluginState();
        throw std::runtime_error{"hypr-radiant: failed to initialize"};
    }

    // Every dispatcher shares one wrapper: the null-plugin guard and the exception handling used to
    // be copy-pasted six times, and two of them (shelf, status) had silently dropped the try/catch,
    // so an exception there would have unwound into Hyprland. Registering through one helper makes
    // that impossible to get inconsistent.
    using PluginMethod = SDispatchResult (hypr_radiant::RadiantPlugin::*)(const std::string&);
    const auto registerDispatcher = [](const char* name, PluginMethod method) {
        return HyprlandAPI::addDispatcherV2(g_pluginHandle, name, [name, method](std::string args) -> SDispatchResult {
            try {
                if (!g_plugin)
                    return {.passEvent = false, .success = false, .error = "hypr-radiant is not initialized"};
                return (g_plugin.get()->*method)(args);
            } catch (const std::exception& error) {
                hypr_radiant::log::error("{} failed: {}", name, error.what());
                return {.passEvent = false, .success = false, .error = error.what()};
            } catch (...) {
                hypr_radiant::log::error("{} failed with an unknown error", name);
                return {.passEvent = false, .success = false, .error = "unknown hypr-radiant dispatcher error"};
            }
        });
    };

    const bool allRegistered = registerDispatcher(DISPATCHER_TOGGLE, &hypr_radiant::RadiantPlugin::toggle)
        && registerDispatcher(DISPATCHER_OPEN, &hypr_radiant::RadiantPlugin::open)
        && registerDispatcher(DISPATCHER_CLOSE, &hypr_radiant::RadiantPlugin::close)
        && registerDispatcher(DISPATCHER_APP, &hypr_radiant::RadiantPlugin::showApplication)
        && registerDispatcher(DISPATCHER_SHELF, &hypr_radiant::RadiantPlugin::shelf)
        && registerDispatcher(DISPATCHER_STATUS, &hypr_radiant::RadiantPlugin::status);

    if (!allRegistered) {
        resetPluginState();
        throw std::runtime_error{"hypr-radiant: failed to register dispatchers"};
    }

    hypr_radiant::log::info("loaded; overview and shelf dispatchers registered");

    return {
        .name        = PLUGIN_NAME,
        .description = PLUGIN_DESCRIPTION,
        .author      = PLUGIN_AUTHOR,
        .version     = PLUGIN_VERSION,
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Hyprland removes plugin-owned dispatchers during unload. The plugin only
    // needs to release its own state and avoid leaving callbacks with live data.
    if (g_plugin)
        hypr_radiant::log::info("unloading");

    resetPluginState();
}

#include <hypr-radiant/RadiantPlugin.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr auto PLUGIN_NAME        = "hypr-radiant";
constexpr auto DISPATCHER_TOGGLE  = "radiant:toggle";
constexpr auto DISPATCHER_APP     = "radiant:app";
constexpr auto DISPATCHER_STATUS  = "radiant:status";
constexpr auto PLUGIN_DESCRIPTION = "Native workspace overview with live previews and search.";
constexpr auto PLUGIN_AUTHOR      = "Nika Sumbadze (@nsumbadze)";
constexpr auto PLUGIN_VERSION     = "0.2.0-dev";
constexpr auto TOGGLE_GUARD_DELAY = std::chrono::milliseconds{300};

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

    m_overlay.install();
    m_input.install(
        [this] { return m_overlay.active(); },
        [this](double x, double y) { return m_overlay.hitTest(x, y); },
        [this](OverviewTarget target) { activate(target, "keyboard activation"); },
        [this](double x, double y) { m_overlay.pointerMoved(x, y); },
        [this](bool pressed, double x, double y) {
            const auto action = m_overlay.pointerButton(pressed, x, y);
            if (action.type == PointerActionType::Activate) {
                activate(action.target, "pointer activation");
                return;
            }
            if (action.type != PointerActionType::MoveWindow)
                return;
            if (!m_activation.moveWindow(action.windowId, action.target.workspaceId, action.target.monitorId)) {
                log::warn("window move target disappeared or was invalid");
                return;
            }
            m_overlay.refresh(m_stateCollector.collect());
        },
        [this](char value) { m_overlay.appendSearchChar(value); },
        [this] { m_overlay.backspaceSearch(); },
        [this](NavigationDirection direction) { m_overlay.moveSelection(direction); },
        [this] { return m_overlay.searchActive(); },
        [this] { m_overlay.beginSearch(); },
        [this](std::int64_t workspaceId) { activate({.type = OverviewTargetType::Workspace, .workspaceId = workspaceId}, "number activation"); },
        [this] {
            const auto wasActive = m_overlay.active();
            m_overlay.clearSearchOrHide();
            if (wasActive && !m_overlay.active()) {
                recordTransition("closed by Escape", true);
                m_input.releaseKeyboard();
            }
        },
        [this] { m_overlay.toggleGroupedMode(); });
    m_gestures.install(
        [this] { return m_config.gestureEnabled(); },
        [this] { return m_config.gestureFingers(); },
        [this] { return m_config.gestureDistance(); },
        [this] { return m_overlay.active(); },
        [this](bool opening) {
            if (!opening)
                return;
            m_overlay.beginGestureOpen(m_stateCollector.collect());
            m_lastOpenedAt = Clock::now();
            m_input.grabKeyboard(false);
        },
        [this](bool opening, double progress) { m_overlay.setGestureProgress(opening, progress); },
        [this](bool opening, bool commit) {
            m_overlay.finishGesture(opening, commit);
            const auto remainsVisible = opening ? commit : !commit;
            if (remainsVisible)
                m_input.grabKeyboard(false);
            else {
                recordTransition(opening ? "opening gesture cancelled" : "closed by gesture", true);
                m_input.releaseKeyboard();
            }
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
    m_overlay.showAppExpose(std::move(state), focused->m_class);
    recordTransition(std::format("opened App Expose for {}", focused->m_class));
    m_lastOpenedAt = Clock::now();
    m_input.grabKeyboard();
    log::info("app expose opened for {}", focused->m_class);
    return {.passEvent = false, .success = true, .error = ""};
}

void RadiantPlugin::shutdown() {
    m_gestures.uninstall();
    m_input.uninstall();
    m_overlay.uninstall();
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
    recordTransition(std::format("closed by {}", source), true);
}

void RadiantPlugin::recordTransition(std::string message, bool notify) {
    m_lastTransition = std::move(message);
    log::info("{}", m_lastTransition);
    if (notify)
        HyprlandAPI::addNotification(m_handle, std::format("[hypr-radiant] {}", m_lastTransition), CHyprColor{0.31F, 0.58F, 0.46F, 1.0F}, 5000);
}

SDispatchResult RadiantPlugin::status(const std::string&) {
    const auto message = std::format("active={} | {}", m_overlay.active() ? "true" : "false", m_lastTransition);
    HyprlandAPI::addNotification(m_handle, std::format("[hypr-radiant] {}", message), CHyprColor{0.31F, 0.58F, 0.46F, 1.0F}, 5000);
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

    m_overlay.toggle(std::move(state));

    if (!wasActive && m_overlay.active()) {
        recordTransition("opened by dispatcher");
        m_lastOpenedAt = now;
        m_input.grabKeyboard();
    } else if (wasActive && !m_overlay.active()) {
        recordTransition("closed by dispatcher toggle", true);
        m_input.releaseKeyboard();
    }

    log::info("radiant overlay {}", m_overlay.active() ? "opened" : "closed");

    return {.passEvent = false, .success = true, .error = ""};
}

bool RadiantPlugin::active() const noexcept {
    return m_overlay.active();
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

    const auto registered = HyprlandAPI::addDispatcherV2(
        g_pluginHandle,
        DISPATCHER_TOGGLE,
        [](std::string args) -> SDispatchResult {
            try {
                if (!g_plugin)
                    return {.passEvent = false, .success = false, .error = "hypr-radiant is not initialized"};

                return g_plugin->toggle(args);
            } catch (const std::exception& error) {
                hypr_radiant::log::error("radiant:toggle failed: {}", error.what());
                return {.passEvent = false, .success = false, .error = error.what()};
            } catch (...) {
                hypr_radiant::log::error("radiant:toggle failed with an unknown error");
                return {.passEvent = false, .success = false, .error = "unknown hypr-radiant dispatcher error"};
            }
        });

    if (!registered) {
        resetPluginState();
        throw std::runtime_error{"hypr-radiant: failed to register dispatcher radiant:toggle"};
    }

    const auto appRegistered = HyprlandAPI::addDispatcherV2(
        g_pluginHandle,
        DISPATCHER_APP,
        [](std::string args) -> SDispatchResult {
            try {
                if (!g_plugin)
                    return {.passEvent = false, .success = false, .error = "hypr-radiant is not initialized"};
                return g_plugin->showApplication(args);
            } catch (const std::exception& error) {
                hypr_radiant::log::error("radiant:app failed: {}", error.what());
                return {.passEvent = false, .success = false, .error = error.what()};
            }
        });

    if (!appRegistered) {
        resetPluginState();
        throw std::runtime_error{"hypr-radiant: failed to register dispatcher radiant:app"};
    }

    const auto statusRegistered = HyprlandAPI::addDispatcherV2(
        g_pluginHandle,
        DISPATCHER_STATUS,
        [](std::string args) -> SDispatchResult {
            if (!g_plugin)
                return {.passEvent = false, .success = false, .error = "hypr-radiant is not initialized"};
            return g_plugin->status(args);
        });
    if (!statusRegistered) {
        resetPluginState();
        throw std::runtime_error{"hypr-radiant: failed to register dispatcher radiant:status"};
    }

    hypr_radiant::log::info("loaded; dispatchers radiant:toggle and radiant:app registered");

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

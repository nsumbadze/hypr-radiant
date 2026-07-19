#include <hypr-radiant/RadiantPlugin.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr auto PLUGIN_NAME        = "hypr-radiant";
constexpr auto DISPATCHER_TOGGLE  = "radiant:toggle";
constexpr auto PLUGIN_DESCRIPTION = "Native workspace overview with live previews and search.";
constexpr auto PLUGIN_AUTHOR      = "Nika Sumbadze (@nsumbadze)";
constexpr auto PLUGIN_VERSION     = "0.1.0";
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
        [this](OverviewTarget target) { activate(target); },
        [this](double x, double y) { m_overlay.selectTargetAt(x, y); },
        [this](char value) { m_overlay.appendSearchChar(value); },
        [this] { m_overlay.backspaceSearch(); },
        [this](NavigationDirection direction) { m_overlay.moveSelection(direction); },
        [this] { return m_overlay.searchActive(); },
        [this] { m_overlay.beginSearch(); },
        [this](std::int64_t workspaceId) { activate({.type = OverviewTargetType::Workspace, .workspaceId = workspaceId}); },
        [this] {
            m_overlay.clearSearchOrHide();
            if (!m_overlay.active())
                m_input.releaseKeyboard();
        });
    return true;
}

void RadiantPlugin::shutdown() {
    m_input.uninstall();
    m_overlay.uninstall();
}

void RadiantPlugin::activate(OverviewTarget target) {
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
        m_lastOpenedAt = now;
        m_input.grabKeyboard();
    } else if (wasActive && !m_overlay.active())
        m_input.releaseKeyboard();

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

    hypr_radiant::log::info("loaded; dispatcher radiant:toggle registered");

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

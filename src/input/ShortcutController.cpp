#include <hypr-radiant/input/ShortcutController.hpp>
#include <hypr-radiant/Log.hpp>

#include <hyprland/src/event/EventBus.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view DEFAULT_MODIFIERS = "SUPER";
constexpr std::string_view DEFAULT_KEY       = "A";
constexpr std::string_view DEFAULT_HANDLER   = "radiant:toggle";

bool keysEqual(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](unsigned char lhs, unsigned char rhs) {
        return std::tolower(lhs) == std::tolower(rhs);
    });
}

} // namespace

namespace hypr_radiant {

void ShortcutController::install(EnabledFn enabled) {
    m_enabled = std::move(enabled);

    m_preReloadListener = Event::bus()->m_events.config.preReload.listen([this] {
        removeBinding();
    });
    m_reloadedListener = Event::bus()->m_events.config.reloaded.listen([this] {
        installBinding();
    });

    installBinding();
}

void ShortcutController::uninstall() {
    m_reloadedListener.reset();
    m_preReloadListener.reset();
    removeBinding();
    m_enabled = {};
}

void ShortcutController::installBinding() {
    removeBinding();

    if (!m_enabled || !m_enabled() || !g_pKeybindManager)
        return;

    const auto modifiers = g_pKeybindManager->stringToModMask(std::string{DEFAULT_MODIFIERS});
    const auto conflict  = std::ranges::any_of(g_pKeybindManager->m_keybinds, [modifiers](const auto& binding) {
        return binding && binding->enabled && binding->modmask == modifiers && binding->keycode == 0 && binding->submap.name.empty() &&
              keysEqual(binding->key, DEFAULT_KEY);
    });

    if (conflict) {
        log::warn("SUPER+A is already bound; leaving the existing shortcut unchanged");
        return;
    }

    m_binding = g_pKeybindManager->addKeybind(SKeybind{
        .key            = std::string{DEFAULT_KEY},
        .modmask     = modifiers,
        .handler        = std::string{DEFAULT_HANDLER},
        .description = "Radiant overview",
        .hasDescription = true,
    });
    log::info("registered default SUPER+A shortcut");
}

void ShortcutController::removeBinding() {
    if (!m_binding)
        return;

    if (g_pKeybindManager) {
        const auto binding = m_binding;
        std::erase(g_pKeybindManager->m_keybinds, binding);
        g_pKeybindManager->shadowKeybinds();
    }

    m_binding.reset();
}

} // namespace hypr_radiant

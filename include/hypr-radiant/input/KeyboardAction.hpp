#pragma once

#include <hypr-radiant/OverviewTarget.hpp>

#include <cstdint>
#include <optional>

namespace hypr_radiant {

enum class KeyboardActionType {
    None,
    Close,
    Activate,
    Backspace,
    OpenSearch,
    TogglePreferences,
    ToggleMode,
    CycleWindow,
    JumpWorkspace,
    Move,
    TextInput,
};

struct KeyboardAction {
    KeyboardActionType  type      = KeyboardActionType::None;
    char                text      = '\0';
    std::int64_t       workspaceId = 0;
    NavigationDirection direction = NavigationDirection::Left;
    int                 step      = 0;
};

struct KeyboardModifiers {
    bool control = false;
    bool shift   = false;
};

struct KeyboardBindings {
    bool vimKeys          = false;
    bool tabCyclesWindows = false;
};

[[nodiscard]] KeyboardAction resolveKeyboardAction(
    std::uint32_t key, bool searching, KeyboardModifiers modifiers, std::optional<char> searchCharacter,
    KeyboardBindings bindings = {});

[[nodiscard]] inline KeyboardAction resolveKeyboardAction(
    std::uint32_t key, bool searching, bool controlHeld, std::optional<char> searchCharacter) {
    return resolveKeyboardAction(key, searching, KeyboardModifiers{.control = controlHeld}, searchCharacter);
}

} // namespace hypr_radiant

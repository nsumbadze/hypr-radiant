#include <hypr-radiant/input/KeyboardAction.hpp>

#include <linux/input-event-codes.h>

namespace hypr_radiant {

KeyboardAction resolveKeyboardAction(
    std::uint32_t key, bool searching, KeyboardModifiers modifiers, std::optional<char> searchCharacter,
    KeyboardBindings bindings) {
    if (key == KEY_ESC)
        return {.type = KeyboardActionType::Close};

    if (key == KEY_ENTER || key == KEY_KPENTER)
        return {.type = KeyboardActionType::Activate};

    if (key == KEY_BACKSPACE)
        return {.type = KeyboardActionType::Backspace};

    // Slash opens an empty search panel first, then behaves like ordinary text inside it.
    if (key == KEY_SLASH && !searching)
        return {.type = KeyboardActionType::OpenSearch};

    if (key == KEY_COMMA && modifiers.control)
        return {.type = KeyboardActionType::TogglePreferences};

    if (key == KEY_TAB) {
        if (searching)
            return {};
        if (!bindings.tabCyclesWindows || modifiers.control)
            return {.type = KeyboardActionType::ToggleMode};
        return {.type = KeyboardActionType::CycleWindow, .step = modifiers.shift ? -1 : 1};
    }

    if (key >= KEY_1 && key <= KEY_9 && !searching) {
        return {
            .type = KeyboardActionType::JumpWorkspace,
            .workspaceId = static_cast<std::int64_t>(1 + (key - KEY_1)),
        };
    }

    // There is no workspace zero, but zero remains valid search text.
    if (key == KEY_0 && !searching)
        return {};

    if (key == KEY_LEFT)
        return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Left};
    if (key == KEY_RIGHT)
        return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Right};
    if (key == KEY_UP)
        return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Up};
    if (key == KEY_DOWN)
        return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Down};

    if (bindings.vimKeys && !searching && !modifiers.control) {
        if (key == KEY_H)
            return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Left};
        if (key == KEY_J)
            return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Down};
        if (key == KEY_K)
            return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Up};
        if (key == KEY_L)
            return {.type = KeyboardActionType::Move, .direction = NavigationDirection::Right};
        if (searchCharacter)
            return {};
    }

    // Ctrl combinations are shortcuts, never fallback text input.
    if (modifiers.control || !searchCharacter)
        return {};

    return {.type = KeyboardActionType::TextInput, .text = *searchCharacter};
}

} // namespace hypr_radiant

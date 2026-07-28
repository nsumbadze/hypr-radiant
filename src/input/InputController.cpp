#include <hypr-radiant/input/InputController.hpp>

#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace hypr_radiant {
namespace {

constexpr auto INPUT_ARM_DELAY = std::chrono::milliseconds{220};
constexpr auto FALLBACK_REPEAT_DELAY = std::chrono::milliseconds{400};
constexpr auto FALLBACK_REPEAT_RATE  = 25;

bool pressed(wl_keyboard_key_state state) {
    return state == WL_KEYBOARD_KEY_STATE_PRESSED;
}

bool pointerPressed(wl_pointer_button_state state) {
    return state == WL_POINTER_BUTTON_STATE_PRESSED;
}

bool ctrlHeld() {
    if (!g_pSeatManager)
        return false;

    const auto keyboard = g_pSeatManager->m_keyboard.lock();
    return keyboard && (keyboard->getModifiers() & HL_MODIFIER_CTRL) != 0;
}

// Touchpads report FINGER/CONTINUOUS scroll, which stays bound to the shelf. Only a physical
// wheel opts into workspace stepping.
bool fromMouseWheel(const IPointer::SAxisEvent& event) {
    return event.mouse || event.source == WL_POINTER_AXIS_SOURCE_WHEEL;
}

std::optional<char> searchCharForKey(uint32_t key) {
    if (g_pSeatManager) {
        const auto keyboard = g_pSeatManager->m_keyboard.lock();
        if (keyboard && keyboard->m_xkbState) {
            std::array<char, 8> text{};
            // Linux input keycodes are evdev values; XKB keycodes carry the historical +8 offset.
            const auto length = xkb_state_key_get_utf8(keyboard->m_xkbState, key + 8, text.data(), text.size());
            if (length == 1) {
                const auto value = static_cast<unsigned char>(text.front());
                if (value >= 0x20 && value <= 0x7E)
                    return static_cast<char>(value);
            }
        }
    }

    // Keep a layout-independent fallback for the brief interval where Hyprland has no active XKB
    // state (for example while a keyboard is being hot-plugged). Linux letter keycodes are not
    // contiguous or alphabetical, so this must be explicit.
    switch (key) {
    case KEY_A:
        return 'a';
    case KEY_B:
        return 'b';
    case KEY_C:
        return 'c';
    case KEY_D:
        return 'd';
    case KEY_E:
        return 'e';
    case KEY_F:
        return 'f';
    case KEY_G:
        return 'g';
    case KEY_H:
        return 'h';
    case KEY_I:
        return 'i';
    case KEY_J:
        return 'j';
    case KEY_K:
        return 'k';
    case KEY_L:
        return 'l';
    case KEY_M:
        return 'm';
    case KEY_N:
        return 'n';
    case KEY_O:
        return 'o';
    case KEY_P:
        return 'p';
    case KEY_Q:
        return 'q';
    case KEY_R:
        return 'r';
    case KEY_S:
        return 's';
    case KEY_T:
        return 't';
    case KEY_U:
        return 'u';
    case KEY_V:
        return 'v';
    case KEY_W:
        return 'w';
    case KEY_X:
        return 'x';
    case KEY_Y:
        return 'y';
    case KEY_Z:
        return 'z';
    case KEY_SPACE:
        return ' ';
        default: return std::nullopt;
    }
}

} // namespace

InputController::~InputController() {
    releaseKeyboard();
}

void InputController::install(Callbacks callbacks) {
    m_active        = std::move(callbacks.active);
    m_activate      = std::move(callbacks.activate);
    m_pointerMove   = std::move(callbacks.pointerMove);
    m_pointerButton = std::move(callbacks.pointerButton);
    m_textInput     = std::move(callbacks.textInput);
    m_backspace     = std::move(callbacks.backspace);
    m_move          = std::move(callbacks.move);
    m_shelfScroll   = std::move(callbacks.shelfScroll);
    m_searchActive  = std::move(callbacks.searchActive);
    m_openSearch    = std::move(callbacks.openSearch);
    m_jump          = std::move(callbacks.jump);
    m_close         = std::move(callbacks.close);
    m_toggleMode    = std::move(callbacks.toggleMode);
    m_togglePreferences = std::move(callbacks.togglePreferences);

    m_mouseMoveListener = Event::bus()->m_events.input.mouse.move.listen([this](Vector2D position, Event::SCallbackInfo& info) {
        if (!m_active || !m_active())
            return;

        info.cancelled = true;
        if (!inputArmed())
            return;

        if (m_pointerMove)
            m_pointerMove(position.x, position.y);
    });

    m_mouseButtonListener = Event::bus()->m_events.input.mouse.button.listen([this](IPointer::SButtonEvent event, Event::SCallbackInfo& info) {
        const auto isPressed = pointerPressed(event.state);
        const auto suppressed = m_openingInputGuard.buttonEvent(event.button, isPressed);
        if (!m_active || !m_active())
            return;

        info.cancelled = shouldCancelInputEvent(isPressed, inputArmed(), suppressed);

        // A fresh primary-button press is already deliberate input. It should not inherit the
        // keyboard repeat delay, which made a quick wall click get swallowed and feel like the
        // workspace required a second click. The opening button itself remains suppressed until
        // release by OpeningInputGuard.
        if (!pointerActivationArmed() || suppressed || event.button != BTN_LEFT)
            return;

        // The matching release can arrive before the general input delay expires. Keep the whole
        // click inside the overlay so clients never receive an orphaned release event.
        info.cancelled = true;

        if (!g_pInputManager)
            return;

        const auto pos = g_pInputManager->getMouseCoordsInternal();
        if (m_pointerButton)
            m_pointerButton(isPressed, pos.x, pos.y);
    });

    m_mouseAxisListener = Event::bus()->m_events.input.mouse.axis.listen([this](IPointer::SAxisEvent event, Event::SCallbackInfo& info) {
        if (!m_active || !m_active())
            return;

        info.cancelled = true;
        if (!inputArmed())
            return;

        if (event.axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
            return;

        const auto amount = event.deltaDiscrete != 0 ? static_cast<double>(event.deltaDiscrete) : event.delta;
        if (std::abs(amount) < 0.01)
            return;

        m_scrollAccumulator += amount;
        const auto threshold = event.deltaDiscrete != 0 ? 1.0 : 10.0;
        if (std::abs(m_scrollAccumulator) < threshold)
            return;

        const auto scrollingUp = m_scrollAccumulator < 0.0;
        m_scrollAccumulator    = 0.0;

        // Ctrl + wheel steps through workspaces, mirroring the horizontal three-finger swipe.
        if (ctrlHeld() && fromMouseWheel(event)) {
            if (m_move)
                m_move(scrollingUp ? NavigationDirection::Left : NavigationDirection::Right);
            return;
        }

        if (m_shelfScroll)
            m_shelfScroll(scrollingUp);
    });

    m_keyListener = Event::bus()->m_events.input.keyboard.key.listen([this](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
        const auto isPressed = pressed(event.state);
        const auto suppressed = m_openingInputGuard.keyEvent(event.keycode, isPressed);
        if (!m_active || !m_active())
            return;

        info.cancelled = shouldCancelInputEvent(isPressed, inputArmed(), suppressed);

        if (!inputArmed() || suppressed)
            return;

        if (event.keycode == KEY_BACKSPACE && !isPressed) {
            stopBackspaceRepeat();
            return;
        }

        if (!isPressed)
            return;

        const auto key = event.keycode;
        if (key == KEY_ESC) {
            if (m_close)
                m_close();
            return;
        }

        if (key == KEY_ENTER || key == KEY_KPENTER) {
            if (!activationArmed())
                return;
            if (m_activate)
                m_activate({});
            return;
        }

        if (key == KEY_BACKSPACE) {
            if (m_backspace)
                m_backspace();
            if (m_searchActive && m_searchActive())
                startBackspaceRepeat();
            return;
        }

        if (key == KEY_SLASH) {
            if (m_openSearch)
                m_openSearch();
            return;
        }

        if (key == KEY_COMMA && ctrlHeld()) {
            if (m_togglePreferences)
                m_togglePreferences();
            return;
        }

        if (key == KEY_TAB) {
            if (!m_searchActive || !m_searchActive()) {
                if (m_toggleMode)
                    m_toggleMode();
            }
            return;
        }

        if (key >= KEY_1 && key <= KEY_9) {
            const auto value = static_cast<char>('1' + (key - KEY_1));
            if (m_searchActive && m_searchActive()) {
                if (m_textInput)
                    m_textInput(value);
            } else if (m_jump) {
                m_jump(static_cast<std::int64_t>(value - '0'));
            }
            return;
        }

        if (key == KEY_0) {
            if (m_searchActive && m_searchActive() && m_textInput)
                m_textInput('0');
            return;
        }

        if (key == KEY_LEFT) {
            if (m_move)
                m_move(NavigationDirection::Left);
        } else if (key == KEY_RIGHT) {
            if (m_move)
                m_move(NavigationDirection::Right);
        } else if (key == KEY_UP) {
            if (m_move)
                m_move(NavigationDirection::Up);
        } else if (key == KEY_DOWN) {
            if (m_move)
                m_move(NavigationDirection::Down);
        } else if (const auto searchChar = searchCharForKey(key)) {
            if (m_textInput)
                m_textInput(*searchChar);
        }
    });

    m_seatGrab = makeShared<CSeatGrab>();
    m_seatGrab->m_keyboard = true;
}

void InputController::grabKeyboard(OpeningRelease openingRelease) {
    const auto wait = openingRelease == OpeningRelease::Wait;
    const auto now = Clock::now();
    m_acceptInputAfter = now + INPUT_ARM_DELAY;
    m_acceptActivationAfter = m_acceptInputAfter;
    m_openingInputGuard.arm(wait);
    if (m_seatGrab && g_pSeatManager)
        g_pSeatManager->setGrab(m_seatGrab);
}

void InputController::releaseKeyboard() {
    stopBackspaceRepeat();
    if (m_seatGrab && g_pSeatManager && g_pSeatManager->m_seatGrab == m_seatGrab)
        g_pSeatManager->setGrab(nullptr);
}

void InputController::deferActivation(std::chrono::milliseconds duration) {
    m_acceptActivationAfter = std::max(m_acceptActivationAfter, Clock::now() + std::max(duration, std::chrono::milliseconds{0}));
}

void InputController::uninstall() {
    releaseKeyboard();
    m_mouseMoveListener.reset();
    m_mouseButtonListener.reset();
    m_mouseAxisListener.reset();
    m_keyListener.reset();
    m_active = {};
    m_activate = {};
    m_pointerMove = {};
    m_pointerButton = {};
    m_textInput = {};
    m_backspace = {};
    m_move = {};
    m_shelfScroll = {};
    m_searchActive = {};
    m_openSearch = {};
    m_jump = {};
    m_close = {};
    m_toggleMode = {};
    m_togglePreferences = {};
    m_scrollAccumulator = 0.0;
    m_acceptInputAfter = Clock::time_point::min();
    m_acceptActivationAfter = Clock::time_point::min();
    m_openingInputGuard.reset();
}

void InputController::startBackspaceRepeat() {
    stopBackspaceRepeat();
    if (!g_pEventLoopManager)
        return;

    auto delay = FALLBACK_REPEAT_DELAY;
    auto rate  = FALLBACK_REPEAT_RATE;
    if (g_pSeatManager) {
        if (const auto keyboard = g_pSeatManager->m_keyboard.lock()) {
            if (keyboard->m_repeatRate <= 0)
                return;
            delay = std::chrono::milliseconds{std::max(0, keyboard->m_repeatDelay)};
            rate  = keyboard->m_repeatRate;
        }
    }
    const auto interval = std::chrono::milliseconds{std::max(1, 1000 / std::max(1, rate))};

    m_backspaceRepeatTimer = makeShared<CEventLoopTimer>(
        delay,
        [this, interval](SP<CEventLoopTimer> self, void*) {
            if (!m_active || !m_active() || !m_searchActive || !m_searchActive()) {
                stopBackspaceRepeat();
                return;
            }
            if (m_backspace)
                m_backspace();
            self->updateTimeout(interval);
        },
        nullptr);
    g_pEventLoopManager->addTimer(m_backspaceRepeatTimer);
}

void InputController::stopBackspaceRepeat() {
    if (!m_backspaceRepeatTimer)
        return;
    m_backspaceRepeatTimer->cancel();
    if (g_pEventLoopManager)
        g_pEventLoopManager->removeTimer(m_backspaceRepeatTimer);
    m_backspaceRepeatTimer.reset();
}

bool InputController::inputArmed() const noexcept {
    return Clock::now() >= m_acceptInputAfter;
}

bool InputController::activationArmed() const noexcept {
    return Clock::now() >= m_acceptActivationAfter && m_openingInputGuard.openingReleaseObserved();
}

bool InputController::pointerActivationArmed() const noexcept {
    return m_openingInputGuard.openingReleaseObserved();
}

} // namespace hypr_radiant

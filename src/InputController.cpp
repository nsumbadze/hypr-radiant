#include <hypr-radiant/InputController.hpp>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include <linux/input-event-codes.h>

#include <cmath>
#include <optional>

namespace hypr_radiant {
namespace {

constexpr auto INPUT_ARM_DELAY = std::chrono::milliseconds{220};
constexpr auto ACTIVATION_ARM_DELAY = std::chrono::milliseconds{700};

bool pressed(wl_keyboard_key_state state) {
    return state == WL_KEYBOARD_KEY_STATE_PRESSED;
}

bool pointerPressed(wl_pointer_button_state state) {
    return state == WL_POINTER_BUTTON_STATE_PRESSED;
}

std::optional<char> searchCharForKey(uint32_t key) {
    if (key >= KEY_A && key <= KEY_Z)
        return static_cast<char>('a' + (key - KEY_A));

    if (key == KEY_SPACE)
        return ' ';

    return std::nullopt;
}

} // namespace

void InputController::install(ActiveFn active, HitTestFn hitTest, ActivateFn activate, PointerMoveFn pointerMove, PointerButtonFn pointerButton, TextInputFn textInput, BackspaceFn backspace,
    MoveFn move, SearchActiveFn searchActive, OpenSearchFn openSearch, JumpFn jump, CloseFn close, ToggleModeFn toggleMode) {
    m_active   = std::move(active);
    m_hitTest  = std::move(hitTest);
    m_activate = std::move(activate);
    m_pointerMove = std::move(pointerMove);
    m_pointerButton = std::move(pointerButton);
    m_textInput = std::move(textInput);
    m_backspace = std::move(backspace);
    m_move     = std::move(move);
    m_searchActive = std::move(searchActive);
    m_openSearch = std::move(openSearch);
    m_jump     = std::move(jump);
    m_close    = std::move(close);
    m_toggleMode = std::move(toggleMode);

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

        info.cancelled = true;

        if (!inputArmed() || !activationArmed() || suppressed || event.button != BTN_LEFT)
            return;

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

        if (event.axis != WL_POINTER_AXIS_VERTICAL_SCROLL || !m_move)
            return;

        const auto amount = event.deltaDiscrete != 0 ? static_cast<double>(event.deltaDiscrete) : event.delta;
        if (std::abs(amount) < 0.01)
            return;

        m_scrollAccumulator += amount;
        const auto threshold = event.deltaDiscrete != 0 ? 1.0 : 10.0;
        if (std::abs(m_scrollAccumulator) < threshold)
            return;

        m_move(m_scrollAccumulator > 0.0 ? NavigationDirection::Right : NavigationDirection::Left);
        m_scrollAccumulator = 0.0;
    });

    m_keyListener = Event::bus()->m_events.input.keyboard.key.listen([this](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
        const auto isPressed = pressed(event.state);
        const auto suppressed = m_openingInputGuard.keyEvent(event.keycode, isPressed);
        if (!m_active || !m_active())
            return;

        info.cancelled = true;

        if (!inputArmed() || suppressed)
            return;

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
            return;
        }

        if (key == KEY_SLASH) {
            if (m_openSearch)
                m_openSearch();
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

void InputController::grabKeyboard(bool waitForOpeningRelease) {
    const auto now = Clock::now();
    m_acceptInputAfter = now + INPUT_ARM_DELAY;
    m_acceptActivationAfter = now + ACTIVATION_ARM_DELAY;
    m_openingInputGuard.arm(waitForOpeningRelease);
    if (m_seatGrab && g_pSeatManager)
        g_pSeatManager->setGrab(m_seatGrab);
}

void InputController::releaseKeyboard() {
    if (m_seatGrab && g_pSeatManager && g_pSeatManager->m_seatGrab == m_seatGrab)
        g_pSeatManager->setGrab(nullptr);
}

void InputController::uninstall() {
    releaseKeyboard();
    m_mouseMoveListener.reset();
    m_mouseButtonListener.reset();
    m_mouseAxisListener.reset();
    m_keyListener.reset();
    m_active = {};
    m_hitTest = {};
    m_activate = {};
    m_pointerMove = {};
    m_pointerButton = {};
    m_textInput = {};
    m_backspace = {};
    m_move = {};
    m_searchActive = {};
    m_openSearch = {};
    m_jump = {};
    m_close = {};
    m_toggleMode = {};
    m_scrollAccumulator = 0.0;
    m_acceptInputAfter = Clock::time_point::min();
    m_acceptActivationAfter = Clock::time_point::min();
    m_openingInputGuard.reset();
}

bool InputController::inputArmed() const noexcept {
    return Clock::now() >= m_acceptInputAfter;
}

bool InputController::activationArmed() const noexcept {
    return Clock::now() >= m_acceptActivationAfter && m_openingInputGuard.openingReleaseObserved();
}

} // namespace hypr_radiant

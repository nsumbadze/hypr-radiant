#include <hypr-radiant/InputController.hpp>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include <linux/input-event-codes.h>

namespace hypr_radiant {
namespace {

constexpr auto XKB_KEYCODE_OFFSET = 8U;

bool pressed(wl_keyboard_key_state state) {
    return state == WL_KEYBOARD_KEY_STATE_PRESSED;
}

bool pointerPressed(wl_pointer_button_state state) {
    return state == WL_POINTER_BUTTON_STATE_PRESSED;
}

uint32_t evdevKeycode(uint32_t hyprlandKeycode) {
    return hyprlandKeycode >= XKB_KEYCODE_OFFSET ? hyprlandKeycode - XKB_KEYCODE_OFFSET : hyprlandKeycode;
}

} // namespace

void InputController::install(ActiveFn active, HitTestFn hitTest, ActivateFn activate, SelectAtFn selectAt, MoveFn move, CloseFn close) {
    m_active   = std::move(active);
    m_hitTest  = std::move(hitTest);
    m_activate = std::move(activate);
    m_selectAt = std::move(selectAt);
    m_move     = std::move(move);
    m_close    = std::move(close);

    m_mouseMoveListener = Event::bus()->m_events.input.mouse.move.listen([this](Vector2D position, Event::SCallbackInfo& info) {
        if (!m_active || !m_active())
            return;

        if (m_selectAt)
            m_selectAt(position.x, position.y);

        info.cancelled = true;
    });

    m_mouseButtonListener = Event::bus()->m_events.input.mouse.button.listen([this](IPointer::SButtonEvent event, Event::SCallbackInfo& info) {
        if (!m_active || !m_active() || !pointerPressed(event.state))
            return;

        if (!g_pInputManager)
            return;

        const auto pos    = g_pInputManager->getMouseCoordsInternal();
        const auto target = m_hitTest ? m_hitTest(pos.x, pos.y) : OverviewTarget{};
        if (target.type != OverviewTargetType::None && m_activate)
            m_activate(target);

        info.cancelled = true;
    });

    m_keyListener = Event::bus()->m_events.input.keyboard.key.listen([this](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
        if (!m_active || !m_active() || !pressed(event.state))
            return;

        const auto key = evdevKeycode(event.keycode);
        if (key == KEY_ESC) {
            if (m_close)
                m_close();
            info.cancelled = true;
            return;
        }

        if (key == KEY_ENTER || key == KEY_KPENTER) {
            if (m_activate)
                m_activate({});
            info.cancelled = true;
            return;
        }

        if (key == KEY_LEFT) {
            if (m_move)
                m_move(NavigationDirection::Left);
            info.cancelled = true;
        } else if (key == KEY_RIGHT) {
            if (m_move)
                m_move(NavigationDirection::Right);
            info.cancelled = true;
        } else if (key == KEY_UP) {
            if (m_move)
                m_move(NavigationDirection::Up);
            info.cancelled = true;
        } else if (key == KEY_DOWN) {
            if (m_move)
                m_move(NavigationDirection::Down);
            info.cancelled = true;
        }
    });
}

void InputController::uninstall() {
    m_mouseMoveListener.reset();
    m_mouseButtonListener.reset();
    m_keyListener.reset();
    m_active = {};
    m_hitTest = {};
    m_activate = {};
    m_selectAt = {};
    m_move = {};
    m_close = {};
}

} // namespace hypr_radiant

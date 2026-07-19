#include <hypr-radiant/GestureController.hpp>

#include <hyprland/src/event/EventBus.hpp>

#include <utility>

namespace hypr_radiant {

void GestureController::install(BoolFn enabled, IntFn fingers, DoubleFn distance, BoolFn overviewActive,
    BeginFn begin, UpdateFn update, EndFn end) {
    m_enabled = std::move(enabled);
    m_fingers = std::move(fingers);
    m_distance = std::move(distance);
    m_overviewActive = std::move(overviewActive);
    m_begin = std::move(begin);
    m_update = std::move(update);
    m_end = std::move(end);

    m_beginListener = Event::bus()->m_events.gesture.swipe.begin.listen([this](IPointer::SSwipeBeginEvent event, Event::SCallbackInfo&) {
        if (!m_enabled || !m_enabled())
            return;
        m_tracker.begin(event.fingers, m_overviewActive && m_overviewActive(), m_fingers ? m_fingers() : 3,
            m_distance ? m_distance() : 300.0);
    });
    m_updateListener = Event::bus()->m_events.gesture.swipe.update.listen([this](IPointer::SSwipeUpdateEvent event, Event::SCallbackInfo& info) {
        const auto state = m_tracker.update(event.delta.x, event.delta.y, event.timeMs);
        if (!state.recognized)
            return;
        info.cancelled = true;
        if (state.justRecognized && m_begin)
            m_begin(state.opening);
        if (m_update)
            m_update(state.opening, state.progress);
    });
    m_endListener = Event::bus()->m_events.gesture.swipe.end.listen([this](IPointer::SSwipeEndEvent event, Event::SCallbackInfo& info) {
        const auto state = m_tracker.end(event.cancelled);
        if (!state.recognized)
            return;
        info.cancelled = true;
        if (m_end)
            m_end(state.opening, state.commit);
    });
}

void GestureController::uninstall() {
    m_endListener.reset();
    m_updateListener.reset();
    m_beginListener.reset();
    m_tracker.reset();
    m_enabled = {};
    m_fingers = {};
    m_distance = {};
    m_overviewActive = {};
    m_begin = {};
    m_update = {};
    m_end = {};
}

} // namespace hypr_radiant

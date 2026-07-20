#pragma once

#include <hypr-radiant/SwipeTracker.hpp>

#include <hyprland/src/helpers/signal/Signal.hpp>

#include <functional>

namespace hypr_radiant {

class GestureController {
  public:
    using BoolFn = std::function<bool()>;
    using IntFn = std::function<int()>;
    using DoubleFn = std::function<double()>;
    using BeginFn = std::function<void(SwipeAction)>;
    using UpdateFn = std::function<void(SwipeAction, double)>;
    using EndFn = std::function<void(SwipeAction, bool)>;

    void install(BoolFn enabled, IntFn fingers, DoubleFn distance, BoolFn overviewActive, BoolFn shelfVisible,
        BeginFn begin, UpdateFn update, EndFn end);
    void uninstall();

  private:
    BoolFn m_enabled;
    IntFn m_fingers;
    DoubleFn m_distance;
    BoolFn m_overviewActive;
    BoolFn m_shelfVisible;
    BeginFn m_begin;
    UpdateFn m_update;
    EndFn m_end;
    SwipeTracker m_tracker;
    CHyprSignalListener m_beginListener;
    CHyprSignalListener m_updateListener;
    CHyprSignalListener m_endListener;
};

} // namespace hypr_radiant

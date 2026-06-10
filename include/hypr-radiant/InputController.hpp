#pragma once

#include <hypr-radiant/HitTester.hpp>

#include <hyprland/src/helpers/signal/Signal.hpp>

#include <functional>

namespace hypr_radiant {

class InputController {
  public:
    using ActiveFn   = std::function<bool()>;
    using HitTestFn  = std::function<OverviewTarget(double, double)>;
    using ActivateFn = std::function<void(OverviewTarget)>;
    using MoveFn     = std::function<void(NavigationDirection)>;
    using CloseFn    = std::function<void()>;

    void install(ActiveFn active, HitTestFn hitTest, ActivateFn activate, MoveFn move, CloseFn close);
    void uninstall();

  private:
    ActiveFn            m_active;
    HitTestFn           m_hitTest;
    ActivateFn          m_activate;
    MoveFn              m_move;
    CloseFn             m_close;
    CHyprSignalListener m_mouseButtonListener;
    CHyprSignalListener m_keyListener;
};

} // namespace hypr_radiant

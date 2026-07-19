#pragma once

#include <hypr-radiant/HitTester.hpp>
#include <hypr-radiant/OpeningInputGuard.hpp>

#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/managers/SeatManager.hpp>

#include <chrono>
#include <functional>

namespace hypr_radiant {

class InputController {
  public:
    using ActiveFn   = std::function<bool()>;
    using HitTestFn  = std::function<OverviewTarget(double, double)>;
    using ActivateFn = std::function<void(OverviewTarget)>;
    using PointerMoveFn = std::function<void(double, double)>;
    using PointerButtonFn = std::function<void(bool, double, double)>;
    using TextInputFn = std::function<void(char)>;
    using BackspaceFn = std::function<void()>;
    using MoveFn     = std::function<void(NavigationDirection)>;
    using SearchActiveFn = std::function<bool()>;
    using OpenSearchFn = std::function<void()>;
    using JumpFn     = std::function<void(std::int64_t)>;
    using CloseFn    = std::function<void()>;
    using ToggleModeFn = std::function<void()>;

    void install(ActiveFn active, HitTestFn hitTest, ActivateFn activate, PointerMoveFn pointerMove, PointerButtonFn pointerButton, TextInputFn textInput, BackspaceFn backspace,
        MoveFn move, SearchActiveFn searchActive, OpenSearchFn openSearch, JumpFn jump, CloseFn close, ToggleModeFn toggleMode);
    void uninstall();

    void grabKeyboard(bool waitForOpeningRelease = true);
    void releaseKeyboard();

  private:
    [[nodiscard]] bool inputArmed() const noexcept;
    [[nodiscard]] bool activationArmed() const noexcept;

    using Clock = std::chrono::steady_clock;

    ActiveFn            m_active;
    HitTestFn           m_hitTest;
    ActivateFn          m_activate;
    PointerMoveFn       m_pointerMove;
    PointerButtonFn     m_pointerButton;
    TextInputFn         m_textInput;
    BackspaceFn         m_backspace;
    MoveFn              m_move;
    SearchActiveFn      m_searchActive;
    OpenSearchFn        m_openSearch;
    JumpFn              m_jump;
    CloseFn             m_close;
    ToggleModeFn        m_toggleMode;
    CHyprSignalListener m_mouseMoveListener;
    CHyprSignalListener m_mouseButtonListener;
    CHyprSignalListener m_mouseAxisListener;
    CHyprSignalListener m_keyListener;
    SP<CSeatGrab>       m_seatGrab;
    double              m_scrollAccumulator = 0.0;
    Clock::time_point   m_acceptInputAfter = Clock::time_point::min();
    Clock::time_point   m_acceptActivationAfter = Clock::time_point::min();
    OpeningInputGuard   m_openingInputGuard;
};

} // namespace hypr_radiant

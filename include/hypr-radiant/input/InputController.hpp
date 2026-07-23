#pragma once

#include <hypr-radiant/OverviewTarget.hpp>
#include <hypr-radiant/input/OpeningInputGuard.hpp>

#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/managers/SeatManager.hpp>

#include <chrono>
#include <functional>

namespace hypr_radiant {

class InputController {
  public:
    // A leaked seat grab has an empty focusable-surface set, so it locks every client out of the
    // keyboard for the rest of the session. Releasing here means that cannot outlive the object even
    // if a future refactor drops the explicit uninstall() call.
    ~InputController();

    using ActiveFn   = std::function<bool()>;
    using ActivateFn = std::function<void(OverviewTarget)>;
    using PointerMoveFn = std::function<void(double, double)>;
    using PointerButtonFn = std::function<void(bool, double, double)>;
    using TextInputFn = std::function<void(char)>;
    using BackspaceFn = std::function<void()>;
    using MoveFn     = std::function<void(NavigationDirection)>;
    using ShelfScrollFn = std::function<void(bool)>;
    using SearchActiveFn = std::function<bool()>;
    using OpenSearchFn = std::function<void()>;
    using JumpFn     = std::function<void(std::int64_t)>;
    using CloseFn    = std::function<void()>;
    using ToggleModeFn = std::function<void()>;

    // One named field per callback: the previous 13 positional std::functions were transposable at
    // the call site — two same-typed lambdas 30 lines apart compiled fine while silently swapping,
    // say, Tab and Escape. Designated initialisers make order irrelevant.
    struct Callbacks {
        ActiveFn        active;
        ActivateFn      activate;
        PointerMoveFn   pointerMove;
        PointerButtonFn pointerButton;
        TextInputFn     textInput;
        BackspaceFn     backspace;
        MoveFn          move;
        ShelfScrollFn   shelfScroll;
        SearchActiveFn  searchActive;
        OpenSearchFn    openSearch;
        JumpFn          jump;
        CloseFn         close;
        ToggleModeFn    toggleMode;
    };

    void install(Callbacks callbacks);
    void uninstall();

    // Whether to hold the opening key (a keybind's own Enter) suppressed until it is released, so it
    // does not immediately activate the selection the overview just opened on.
    enum class OpeningRelease { Wait,
        Skip };
    void grabKeyboard(OpeningRelease openingRelease = OpeningRelease::Wait);
    void releaseKeyboard();
    void deferActivation(std::chrono::milliseconds duration);

  private:
    [[nodiscard]] bool inputArmed() const noexcept;
    [[nodiscard]] bool activationArmed() const noexcept;

    using Clock = std::chrono::steady_clock;

    ActiveFn            m_active;
    ActivateFn          m_activate;
    PointerMoveFn       m_pointerMove;
    PointerButtonFn     m_pointerButton;
    TextInputFn         m_textInput;
    BackspaceFn         m_backspace;
    MoveFn              m_move;
    ShelfScrollFn       m_shelfScroll;
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

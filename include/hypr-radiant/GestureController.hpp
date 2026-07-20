#pragma once

#include <hyprland/src/helpers/signal/Signal.hpp>

#include <cstdint>
#include <functional>

namespace hypr_radiant {

enum class SwipeAction {
    None,
    OpenOverview,
    CloseOverview,
    RevealShelf,
    HideShelf,
    PreviousWorkspace,
    NextWorkspace,
};

struct SwipeUpdate {
    bool recognized = false;
    bool justRecognized = false;
    SwipeAction action = SwipeAction::None;
    double progress = 0.0;
};

struct SwipeEnd {
    bool recognized = false;
    SwipeAction action = SwipeAction::None;
    bool commit = false;
};

class SwipeTracker {
  public:
    void begin(std::uint32_t fingers, bool overviewActive, bool shelfVisible, int requiredFingers, double distance);
    [[nodiscard]] SwipeUpdate update(double deltaX, double deltaY, std::uint32_t timeMs);
    [[nodiscard]] SwipeEnd end(bool cancelled);
    void reset();

  private:
    bool m_tracking = false;
    bool m_recognized = false;
    bool m_overviewActive = false;
    bool m_shelfVisible = false;
    SwipeAction m_action = SwipeAction::None;
    double m_distance = 300.0;
    double m_totalX = 0.0;
    double m_totalY = 0.0;
    double m_velocityY = 0.0;
    double m_velocityX = 0.0;
    std::uint32_t m_lastTimeMs = 0;
};

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

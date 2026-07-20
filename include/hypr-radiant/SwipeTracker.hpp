#pragma once

#include <cstdint>

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

} // namespace hypr_radiant

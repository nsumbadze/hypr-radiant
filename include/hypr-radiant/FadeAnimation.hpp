#pragma once

#include <chrono>

namespace hypr_radiant {

class FadeAnimation {
  public:
    void animateTo(bool visible, int durationMs);
    void hideImmediate();

    [[nodiscard]] double value();
    [[nodiscard]] bool   targetVisible() const noexcept;
    [[nodiscard]] bool   running();
    [[nodiscard]] bool   renderable();

  private:
    using Clock = std::chrono::steady_clock;

    void update(Clock::time_point now);

    Clock::time_point         m_startedAt = Clock::now();
    std::chrono::milliseconds m_duration{0};
    double                    m_startValue    = 0.0;
    double                    m_currentValue  = 0.0;
    double                    m_targetValue   = 0.0;
    bool                      m_targetVisible = false;
    bool                      m_running       = false;
};

} // namespace hypr_radiant

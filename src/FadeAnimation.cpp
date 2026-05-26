#include <hypr-radiant/FadeAnimation.hpp>

#include <algorithm>
#include <cmath>

namespace hypr_radiant {

void FadeAnimation::animateTo(bool visible, int durationMs) {
    const auto now = Clock::now();
    update(now);

    m_startedAt     = now;
    m_duration      = std::chrono::milliseconds{std::max(0, durationMs)};
    m_startValue    = m_currentValue;
    m_targetValue   = visible ? 1.0 : 0.0;
    m_targetVisible = visible;
    m_running       = m_duration.count() > 0 && std::abs(m_targetValue - m_startValue) > 0.001;

    if (!m_running)
        m_currentValue = m_targetValue;
}

void FadeAnimation::hideImmediate() {
    m_startValue    = 0.0;
    m_currentValue  = 0.0;
    m_targetValue   = 0.0;
    m_targetVisible = false;
    m_running       = false;
    m_duration      = std::chrono::milliseconds{0};
}

double FadeAnimation::value() {
    update(Clock::now());
    return m_currentValue;
}

bool FadeAnimation::targetVisible() const noexcept {
    return m_targetVisible;
}

bool FadeAnimation::running() {
    update(Clock::now());
    return m_running;
}

bool FadeAnimation::renderable() {
    update(Clock::now());
    return m_running || m_currentValue > 0.001;
}

void FadeAnimation::update(Clock::time_point now) {
    if (!m_running)
        return;

    const auto elapsed = std::chrono::duration<double, std::milli>(now - m_startedAt).count();
    const auto total   = static_cast<double>(m_duration.count());

    if (total <= 0.0 || elapsed >= total) {
        m_currentValue = m_targetValue;
        m_running      = false;
        return;
    }

    const auto linear = std::clamp(elapsed / total, 0.0, 1.0);
    const auto eased  = linear * linear * (3.0 - 2.0 * linear);

    m_currentValue = std::lerp(m_startValue, m_targetValue, eased);
}

} // namespace hypr_radiant

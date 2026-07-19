#include <hypr-radiant/GestureController.hpp>

#include <algorithm>
#include <cmath>

namespace hypr_radiant {

void SwipeTracker::begin(std::uint32_t fingers, bool overviewActive, int requiredFingers, double distance) {
    reset();
    if (fingers != static_cast<std::uint32_t>(requiredFingers))
        return;
    m_tracking = true;
    m_opening = !overviewActive;
    m_distance = std::max(1.0, distance);
}

SwipeUpdate SwipeTracker::update(double deltaX, double deltaY, std::uint32_t timeMs) {
    if (!m_tracking)
        return {};
    m_totalX += deltaX;
    m_totalY += deltaY;
    if (m_lastTimeMs != 0 && timeMs > m_lastTimeMs)
        m_velocityY = deltaY / static_cast<double>(timeMs - m_lastTimeMs);
    m_lastTimeMs = timeMs;

    bool justRecognized = false;
    if (!m_recognized && std::abs(m_totalY) >= 12.0 && std::abs(m_totalY) > std::abs(m_totalX) * 1.2) {
        const auto directionMatches = m_opening ? m_totalY < 0.0 : m_totalY > 0.0;
        if (!directionMatches) {
            m_tracking = false;
            return {};
        }
        m_recognized = true;
        justRecognized = true;
    }
    if (!m_recognized)
        return {};

    const auto directionalDistance = m_opening ? -m_totalY : m_totalY;
    return {.recognized = true, .justRecognized = justRecognized, .opening = m_opening,
        .progress = std::clamp(directionalDistance / m_distance, 0.0, 1.0)};
}

SwipeEnd SwipeTracker::end(bool cancelled) {
    SwipeEnd result;
    if (m_recognized) {
        const auto directionalDistance = m_opening ? -m_totalY : m_totalY;
        const auto directionalVelocity = m_opening ? -m_velocityY : m_velocityY;
        result = {.recognized = true, .opening = m_opening,
            .commit = !cancelled && (directionalDistance / m_distance >= 0.42 || directionalVelocity >= 0.65)};
    }
    reset();
    return result;
}

void SwipeTracker::reset() {
    m_tracking = false;
    m_recognized = false;
    m_opening = false;
    m_distance = 300.0;
    m_totalX = 0.0;
    m_totalY = 0.0;
    m_velocityY = 0.0;
    m_lastTimeMs = 0;
}

} // namespace hypr_radiant

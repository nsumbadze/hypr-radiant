#include <hypr-radiant/SwipeTracker.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hypr_radiant {

void SwipeTracker::begin(std::uint32_t fingers, bool overviewActive, bool shelfVisible, int requiredFingers, double distance) {
    reset();
    if (fingers != static_cast<std::uint32_t>(requiredFingers))
        return;
    m_tracking = true;
    m_overviewActive = overviewActive;
    m_shelfVisible = shelfVisible;
    m_distance = std::max(1.0, distance);
}

SwipeUpdate SwipeTracker::update(double deltaX, double deltaY, std::uint32_t timeMs) {
    if (!m_tracking)
        return {};
    m_totalX += deltaX;
    m_totalY += deltaY;
    if (m_lastTimeMs != 0 && timeMs > m_lastTimeMs) {
        m_velocityX = deltaX / static_cast<double>(timeMs - m_lastTimeMs);
        m_velocityY = deltaY / static_cast<double>(timeMs - m_lastTimeMs);
    }
    m_lastTimeMs = timeMs;

    bool justRecognized = false;
    const auto horizontal = std::abs(m_totalX) >= 12.0 && std::abs(m_totalX) > std::abs(m_totalY) * 1.2;
    const auto vertical = std::abs(m_totalY) >= 12.0 && std::abs(m_totalY) > std::abs(m_totalX) * 1.2;
    if (!m_recognized && (horizontal || vertical)) {
        if (!m_overviewActive) {
            if (!vertical || m_totalY >= 0.0) {
                m_tracking = false;
                return {};
            }
            m_action = SwipeAction::OpenOverview;
        } else if (horizontal) {
            m_action = m_totalX > 0.0 ? SwipeAction::PreviousWorkspace : SwipeAction::NextWorkspace;
        } else if (m_totalY < 0.0) {
            m_action = SwipeAction::RevealShelf;
        } else {
            m_action = m_shelfVisible ? SwipeAction::HideShelf : SwipeAction::CloseOverview;
        }
        m_recognized = true;
        justRecognized = true;
    }
    if (!m_recognized)
        return {};

    const auto directionalDistance = m_action == SwipeAction::PreviousWorkspace ? m_totalX :
        m_action == SwipeAction::NextWorkspace ? -m_totalX :
        m_action == SwipeAction::OpenOverview || m_action == SwipeAction::RevealShelf ? -m_totalY : m_totalY;
    return {.recognized = true, .justRecognized = justRecognized, .action = m_action,
        .progress = std::clamp(directionalDistance / m_distance, 0.0, 1.0)};
}

SwipeEnd SwipeTracker::end(bool cancelled) {
    SwipeEnd result;
    if (m_recognized) {
        const auto directionalDistance = m_action == SwipeAction::PreviousWorkspace ? m_totalX :
            m_action == SwipeAction::NextWorkspace ? -m_totalX :
            m_action == SwipeAction::OpenOverview || m_action == SwipeAction::RevealShelf ? -m_totalY : m_totalY;
        const auto directionalVelocity = m_action == SwipeAction::PreviousWorkspace ? m_velocityX :
            m_action == SwipeAction::NextWorkspace ? -m_velocityX :
            m_action == SwipeAction::OpenOverview || m_action == SwipeAction::RevealShelf ? -m_velocityY : m_velocityY;
        const auto threshold = m_action == SwipeAction::OpenOverview || m_action == SwipeAction::CloseOverview ? 0.42 : 0.24;
        result = {.recognized = true, .action = m_action,
            .commit = !cancelled && (directionalDistance / m_distance >= threshold || directionalVelocity >= 0.65)};
    }
    reset();
    return result;
}

void SwipeTracker::reset() {
    m_tracking = false;
    m_recognized = false;
    m_overviewActive = false;
    m_shelfVisible = false;
    m_action = SwipeAction::None;
    m_distance = 300.0;
    m_totalX = 0.0;
    m_totalY = 0.0;
    m_velocityY = 0.0;
    m_velocityX = 0.0;
    m_lastTimeMs = 0;
}

} // namespace hypr_radiant

#include <hypr-radiant/OpeningInputGuard.hpp>

namespace hypr_radiant {

bool OpeningInputGuard::keyEvent(std::uint32_t key, bool pressed) {
    return update(m_heldKeys, m_suppressedKeys, key, pressed);
}

bool OpeningInputGuard::buttonEvent(std::uint32_t button, bool pressed) {
    return update(m_heldButtons, m_suppressedButtons, button, pressed);
}

void OpeningInputGuard::arm(bool waitForRelease) {
    m_suppressedKeys = m_heldKeys;
    m_suppressedButtons = m_heldButtons;
    m_openingReleaseObserved = !waitForRelease;
}

void OpeningInputGuard::reset() {
    m_heldKeys.clear();
    m_suppressedKeys.clear();
    m_heldButtons.clear();
    m_suppressedButtons.clear();
    m_openingReleaseObserved = true;
}

bool OpeningInputGuard::openingReleaseObserved() const noexcept {
    return m_openingReleaseObserved;
}

bool OpeningInputGuard::update(std::unordered_set<std::uint32_t>& held, std::unordered_set<std::uint32_t>& suppressed,
    std::uint32_t code, bool pressed) {
    const auto shouldSuppress = suppressed.contains(code);
    if (pressed) {
        held.insert(code);
    } else {
        held.erase(code);
        suppressed.erase(code);
        m_openingReleaseObserved = true;
    }
    return shouldSuppress;
}

} // namespace hypr_radiant

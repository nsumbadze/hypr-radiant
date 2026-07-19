#pragma once

#include <cstdint>
#include <unordered_set>

namespace hypr_radiant {

class OpeningInputGuard {
  public:
    [[nodiscard]] bool keyEvent(std::uint32_t key, bool pressed);
    [[nodiscard]] bool buttonEvent(std::uint32_t button, bool pressed);
    void arm(bool waitForRelease = true);
    void reset();
    [[nodiscard]] bool openingReleaseObserved() const noexcept;

  private:
    bool update(std::unordered_set<std::uint32_t>& held, std::unordered_set<std::uint32_t>& suppressed,
        std::uint32_t code, bool pressed);

    std::unordered_set<std::uint32_t> m_heldKeys;
    std::unordered_set<std::uint32_t> m_suppressedKeys;
    std::unordered_set<std::uint32_t> m_heldButtons;
    std::unordered_set<std::uint32_t> m_suppressedButtons;
    bool m_openingReleaseObserved = true;
};

} // namespace hypr_radiant

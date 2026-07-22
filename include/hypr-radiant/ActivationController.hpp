#pragma once

#include <hypr-radiant/HitTester.hpp>

namespace hypr_radiant {

class ActivationController {
  public:
    [[nodiscard]] bool activate(const OverviewTarget& target) const;
    [[nodiscard]] bool moveWindow(std::uint64_t windowId, std::int64_t workspaceId, std::int64_t monitorId) const;
    [[nodiscard]] bool closeWindow(std::uint64_t windowId) const;
};

} // namespace hypr_radiant

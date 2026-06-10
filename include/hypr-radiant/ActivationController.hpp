#pragma once

#include <hypr-radiant/HitTester.hpp>

namespace hypr_radiant {

class ActivationController {
  public:
    [[nodiscard]] bool activate(const OverviewTarget& target) const;
};

} // namespace hypr_radiant

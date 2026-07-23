#pragma once

#include <hypr-radiant/RadiantState.hpp>

namespace hypr_radiant {

class StateCollector {
  public:
    [[nodiscard]] RadiantState collect() const;
};

} // namespace hypr_radiant

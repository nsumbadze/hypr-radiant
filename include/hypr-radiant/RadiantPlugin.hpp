#pragma once

#include <hyprland/src/SharedDefs.hpp>

#include <string>

namespace hypr_radiant {

class RadiantPlugin {
  public:
    SDispatchResult toggle(const std::string& args);
    [[nodiscard]] bool active() const noexcept;

  private:
    bool m_active = false;
};

} // namespace hypr_radiant

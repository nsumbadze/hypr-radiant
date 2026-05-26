#pragma once

#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace hypr_radiant {

class RadiantConfig {
  public:
    bool registerValues(HANDLE handle);

    [[nodiscard]] float opacity() const;
    [[nodiscard]] int   animationDurationMs() const;

  private:
    SP<Config::Values::CFloatValue> m_opacity;
    SP<Config::Values::CIntValue>   m_animationDurationMs;
};

} // namespace hypr_radiant

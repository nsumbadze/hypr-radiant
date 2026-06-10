#pragma once

#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace hypr_radiant {

enum class LayoutMode {
    WorkspaceWall,
};

class RadiantConfig {
  public:
    bool registerValues(HANDLE handle);

    [[nodiscard]] float           opacity() const;
    [[nodiscard]] int             animationDurationMs() const;
    [[nodiscard]] LayoutMode layoutMode() const;

  private:
    SP<Config::Values::CFloatValue>  m_opacity;
    SP<Config::Values::CIntValue>    m_animationDurationMs;
    SP<Config::Values::CStringValue> m_layout;
};

} // namespace hypr_radiant

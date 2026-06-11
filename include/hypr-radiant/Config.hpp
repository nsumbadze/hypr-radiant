#pragma once

#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string_view>

namespace hypr_radiant {

enum class LayoutMode {
    Stage,
    WorkspaceWall,
};

[[nodiscard]] LayoutMode parseLayoutMode(std::string_view value);

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

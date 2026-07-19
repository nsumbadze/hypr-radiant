#pragma once

#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string_view>
#include <optional>

namespace hypr_radiant {

enum class LayoutMode {
    Stage,
    WorkspaceWall,
};

[[nodiscard]] LayoutMode parseLayoutMode(std::string_view value);

struct RadiantRgba {
    float red   = 0.0F;
    float green = 0.0F;
    float blue  = 0.0F;
    float alpha = 1.0F;
};

[[nodiscard]] std::optional<RadiantRgba> parseAccentColor(std::string_view value);

class RadiantConfig {
  public:
    bool registerValues(HANDLE handle);

    [[nodiscard]] float           opacity() const;
    [[nodiscard]] int             animationDurationMs() const;
    [[nodiscard]] LayoutMode layoutMode() const;
    [[nodiscard]] std::optional<CHyprColor> accentColorOverride() const;
    [[nodiscard]] CHyprColor       backgroundColor() const;
    [[nodiscard]] CHyprColor       foregroundColor() const;
    [[nodiscard]] std::string      fontFamily() const;
    [[nodiscard]] bool            gestureEnabled() const;
    [[nodiscard]] int             gestureFingers() const;
    [[nodiscard]] double          gestureDistance() const;

  private:
    SP<Config::Values::CFloatValue>  m_opacity;
    SP<Config::Values::CIntValue>    m_animationDurationMs;
    SP<Config::Values::CStringValue> m_layout;
    SP<Config::Values::CStringValue> m_accentColor;
    SP<Config::Values::CStringValue> m_backgroundColor;
    SP<Config::Values::CStringValue> m_foregroundColor;
    SP<Config::Values::CStringValue> m_fontFamily;
    SP<Config::Values::CIntValue>    m_gestureEnabled;
    SP<Config::Values::CIntValue>    m_gestureFingers;
    SP<Config::Values::CFloatValue>  m_gestureDistance;
};

} // namespace hypr_radiant

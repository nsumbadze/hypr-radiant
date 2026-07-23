#pragma once

#include <hypr-radiant/config/Color.hpp>
#include <hypr-radiant/config/OmarchyPalette.hpp>

#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string_view>
#include <optional>

namespace hypr_radiant {

inline constexpr bool DEFAULT_GESTURE_ENABLED = true;
inline constexpr int DEFAULT_GESTURE_FINGERS = 3;
inline constexpr double DEFAULT_GESTURE_DISTANCE = 300.0;

enum class LayoutMode {
    Stage,
    WorkspaceWall,
};

[[nodiscard]] LayoutMode parseLayoutMode(std::string_view value);

class RadiantConfig {
  public:
    bool registerValues(HANDLE handle);

    /// Re-reads the active Omarchy theme palette. Called when the overview opens so a theme
    /// switch is picked up without reloading the plugin.
    void refreshPalette();
    [[nodiscard]] const OmarchyPalette& palette() const;

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
    OmarchyPalette                   m_palette;
};

} // namespace hypr_radiant

#pragma once

#include <hypr-radiant/config/Color.hpp>
#include <hypr-radiant/config/OmarchyPalette.hpp>
#include <hypr-radiant/render/ChromeStyle.hpp>

#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include <string_view>
#include <optional>
#include <string>

namespace hypr_radiant {

inline constexpr bool DEFAULT_GESTURE_ENABLED = true;
inline constexpr int DEFAULT_GESTURE_FINGERS = 3;
inline constexpr double DEFAULT_GESTURE_DISTANCE = 300.0;
inline constexpr bool DEFAULT_SHORTCUT_ENABLED = true;

enum class LayoutMode {
    Stage,
    WorkspaceWall,
    Carousel,
    Ribbon,
};

enum class ShelfMode { Auto, Always, Hidden };
enum class WindowNavigation { List, Spatial };

[[nodiscard]] LayoutMode parseLayoutMode(std::string_view value);
[[nodiscard]] ShelfMode  parseShelfMode(std::string_view value);
[[nodiscard]] WindowNavigation parseWindowNavigation(std::string_view value);

class RadiantConfig {
  public:
    bool registerValues(HANDLE handle);
    [[nodiscard]] const std::string& registrationError() const noexcept;

    /// Re-reads the desktop's active Omarchy palette, or an installed named theme for Radiant
    /// only. Called when the overview opens so installed and active theme changes need no reload.
    void refreshPalette(std::string_view themeSlug = {});
    [[nodiscard]] const OmarchyPalette& palette() const;

    [[nodiscard]] float           opacity() const;
    [[nodiscard]] int             animationDurationMs() const;
    [[nodiscard]] LayoutMode layoutMode() const;
    [[nodiscard]] CHyprColor       backgroundColor() const;
    [[nodiscard]] CHyprColor       foregroundColor() const;
    [[nodiscard]] std::string      fontFamily() const;
    [[nodiscard]] bool            gestureEnabled() const;
    [[nodiscard]] int             gestureFingers() const;
    [[nodiscard]] double          gestureDistance() const;
    [[nodiscard]] bool            shortcutEnabled() const;
    [[nodiscard]] ChromePreset    chromePreset() const;
    [[nodiscard]] int             roundingOverride() const;
    [[nodiscard]] int             borderSizeOverride() const;
    [[nodiscard]] std::optional<RadiantRgba> borderColorOverride() const;
    [[nodiscard]] EffectsMode     effectsMode() const;
    [[nodiscard]] double          spacing() const;
    [[nodiscard]] ShelfMode       shelfMode() const;
    [[nodiscard]] WindowNavigation windowNavigation() const;
    [[nodiscard]] bool             vimKeys() const;
    [[nodiscard]] bool             tabCyclesWindows() const;

  private:
    SP<Config::Values::CFloatValue>  m_opacity;
    SP<Config::Values::CIntValue>    m_animationDurationMs;
    SP<Config::Values::CStringValue> m_layout;
    SP<Config::Values::CStringValue> m_backgroundColor;
    SP<Config::Values::CStringValue> m_foregroundColor;
    SP<Config::Values::CStringValue> m_fontFamily;
    SP<Config::Values::CIntValue>    m_gestureEnabled;
    SP<Config::Values::CIntValue>    m_gestureFingers;
    SP<Config::Values::CFloatValue>  m_gestureDistance;
    SP<Config::Values::CIntValue>    m_shortcutEnabled;
    SP<Config::Values::CStringValue> m_chrome;
    SP<Config::Values::CIntValue>    m_rounding;
    SP<Config::Values::CIntValue>    m_borderSize;
    SP<Config::Values::CStringValue> m_borderColor;
    SP<Config::Values::CStringValue> m_effects;
    SP<Config::Values::CFloatValue>  m_spacing;
    SP<Config::Values::CStringValue> m_shelf;
    SP<Config::Values::CStringValue> m_windowNavigation;
    SP<Config::Values::CIntValue>    m_vimKeys;
    SP<Config::Values::CIntValue>    m_tabCyclesWindows;
    OmarchyPalette                   m_palette;
    std::string                      m_registrationError;
};

} // namespace hypr_radiant

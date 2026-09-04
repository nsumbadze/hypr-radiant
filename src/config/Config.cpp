#include <hypr-radiant/config/Config.hpp>
#include <hypr-radiant/Log.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

namespace hypr_radiant {

bool RadiantConfig::registerValues(HANDLE handle) {
    m_registrationError.clear();
    m_opacity = makeShared<Config::Values::CFloatValue>(
        "plugin:radiant:opacity",
        "Fullscreen overlay opacity.",
        0.94F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});

    m_animationDurationMs = makeShared<Config::Values::CIntValue>(
        "plugin:radiant:animation_duration",
        "Overlay fade animation duration in milliseconds.",
        180,
        Config::Values::SIntValueOptions{.min = 0, .max = 2000});

    m_layout = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:layout",
        "Overview layout mode.",
        "stage");

    m_backgroundColor = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:background_color", "Overview glass background color, or auto to follow the Omarchy theme.", "auto");
    m_foregroundColor = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:foreground_color", "Overview text color, or auto to follow the Omarchy theme.", "auto");
    m_fontFamily = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:font_family", "Overview interface font family.", "JetBrainsMono Nerd Font");

    m_gestureEnabled = makeShared<Config::Values::CIntValue>(
        "plugin:radiant:gesture_enabled", "Enable interactive overview trackpad gestures.", DEFAULT_GESTURE_ENABLED ? 1 : 0,
        Config::Values::SIntValueOptions{.min = 0, .max = 1});
    m_gestureFingers = makeShared<Config::Values::CIntValue>(
        "plugin:radiant:gesture_fingers", "Trackpad finger count for overview gestures.", DEFAULT_GESTURE_FINGERS,
        Config::Values::SIntValueOptions{.min = 3, .max = 4});
    m_gestureDistance = makeShared<Config::Values::CFloatValue>(
        "plugin:radiant:gesture_distance", "Trackpad travel required to fully open the overview.", static_cast<float>(DEFAULT_GESTURE_DISTANCE),
        Config::Values::SFloatValueOptions{.min = 120.0F, .max = 800.0F});
    m_shortcutEnabled = makeShared<Config::Values::CIntValue>(
        "plugin:radiant:shortcut_enabled", "Register SUPER+A as the default overview shortcut.", DEFAULT_SHORTCUT_ENABLED ? 1 : 0,
        Config::Values::SIntValueOptions{.min = 0, .max = 1});
    m_chrome = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:chrome", "Overlay chrome preset: radiant, native, or flat.", "radiant");
    m_rounding = makeShared<Config::Values::CIntValue>(
        "plugin:radiant:rounding", "Overlay corner radius, or -1 to follow the chrome preset.", -1,
        Config::Values::SIntValueOptions{.min = -1, .max = 40});
    m_borderSize = makeShared<Config::Values::CIntValue>(
        "plugin:radiant:border_size", "Overlay border size, or -1 to follow the chrome preset.", -1,
        Config::Values::SIntValueOptions{.min = -1, .max = 12});
    m_borderColor = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:border_color", "Selected border color, or auto to follow the chrome preset.", "auto");
    m_effects = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:effects", "Overlay shadows, glow, and blur: auto, on, or off.", "auto");
    m_spacing = makeShared<Config::Values::CFloatValue>(
        "plugin:radiant:spacing", "Multiplier for overview card padding and gaps.", 1.0F,
        Config::Values::SFloatValueOptions{.min = 0.5F, .max = 2.0F});
    m_shelf = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:shelf", "Stage workspace shelf behavior: auto, always, or hidden.", "auto");

    refreshPalette();

    const std::array<SP<Config::Values::IValue>, 17> values{
        m_opacity,
        m_animationDurationMs,
        m_layout,
        m_backgroundColor,
        m_foregroundColor,
        m_fontFamily,
        m_gestureEnabled,
        m_gestureFingers,
        m_gestureDistance,
        m_shortcutEnabled,
        m_chrome,
        m_rounding,
        m_borderSize,
        m_borderColor,
        m_effects,
        m_spacing,
        m_shelf,
    };

    for (const auto& value : values) {
        if (HyprlandAPI::addConfigValueV2(handle, value))
            continue;

        m_registrationError = std::format(
            "failed to register Hyprland option {}; rebuild hypr-radiant against the running compositor headers and libraries",
            value->name());
        log::error("{}", m_registrationError);
        HyprlandAPI::addNotification(
            handle, std::format("[hypr-radiant] {}", m_registrationError), CHyprColor{1.0F, 0.2F, 0.2F, 1.0F}, 7000);
        return false;
    }

    return true;
}

const std::string& RadiantConfig::registrationError() const noexcept {
    return m_registrationError;
}

bool RadiantConfig::gestureEnabled() const {
    return !m_gestureEnabled ? DEFAULT_GESTURE_ENABLED : m_gestureEnabled->value() != 0;
}

int RadiantConfig::gestureFingers() const {
    if (!m_gestureFingers)
        return DEFAULT_GESTURE_FINGERS;
    return static_cast<int>(std::clamp(m_gestureFingers->value(), static_cast<Config::INTEGER>(3), static_cast<Config::INTEGER>(4)));
}

double RadiantConfig::gestureDistance() const {
    if (!m_gestureDistance)
        return DEFAULT_GESTURE_DISTANCE;
    return std::clamp(static_cast<double>(m_gestureDistance->value()), 120.0, 800.0);
}

bool RadiantConfig::shortcutEnabled() const {
    return !m_shortcutEnabled ? DEFAULT_SHORTCUT_ENABLED : m_shortcutEnabled->value() != 0;
}

ChromePreset RadiantConfig::chromePreset() const {
    return m_chrome ? parseChromePreset(m_chrome->value()) : ChromePreset::Radiant;
}

int RadiantConfig::roundingOverride() const {
    return m_rounding ? static_cast<int>(std::clamp(m_rounding->value(), static_cast<Config::INTEGER>(-1), static_cast<Config::INTEGER>(40))) : -1;
}

int RadiantConfig::borderSizeOverride() const {
    return m_borderSize ? static_cast<int>(std::clamp(m_borderSize->value(), static_cast<Config::INTEGER>(-1), static_cast<Config::INTEGER>(12))) : -1;
}

std::optional<RadiantRgba> RadiantConfig::borderColorOverride() const {
    return m_borderColor ? parseAccentColor(m_borderColor->value()) : std::nullopt;
}

EffectsMode RadiantConfig::effectsMode() const {
    return m_effects ? parseEffectsMode(m_effects->value()) : EffectsMode::Auto;
}

double RadiantConfig::spacing() const {
    return m_spacing ? std::clamp(static_cast<double>(m_spacing->value()), 0.5, 2.0) : 1.0;
}

ShelfMode RadiantConfig::shelfMode() const {
    return m_shelf ? parseShelfMode(m_shelf->value()) : ShelfMode::Auto;
}

float RadiantConfig::opacity() const {
    if (!m_opacity)
        return 0.94F;

    return std::clamp(m_opacity->value(), 0.0F, 1.0F);
}

int RadiantConfig::animationDurationMs() const {
    if (!m_animationDurationMs)
        return 180;

    return static_cast<int>(std::clamp(m_animationDurationMs->value(), static_cast<Config::INTEGER>(0), static_cast<Config::INTEGER>(2000)));
}

LayoutMode RadiantConfig::layoutMode() const {
    if (!m_layout)
        return LayoutMode::Stage;

    return parseLayoutMode(m_layout->value());
}

void RadiantConfig::refreshPalette(std::string_view themeSlug) {
    m_palette = loadOmarchyPalette(themeSlug);
}

const OmarchyPalette& RadiantConfig::palette() const {
    return m_palette;
}

// An explicit config value wins; `auto` falls through to the active Omarchy theme, which itself
// falls back to neutral gray when no theme file is readable.
CHyprColor RadiantConfig::backgroundColor() const {
    const auto parsed = m_backgroundColor ? parseAccentColor(m_backgroundColor->value()) : std::nullopt;
    const auto color  = parsed.value_or(m_palette.background);
    return {color.red, color.green, color.blue, color.alpha};
}

CHyprColor RadiantConfig::foregroundColor() const {
    const auto parsed = m_foregroundColor ? parseAccentColor(m_foregroundColor->value()) : std::nullopt;
    const auto color  = parsed.value_or(m_palette.foreground);
    return {color.red, color.green, color.blue, color.alpha};
}

std::string RadiantConfig::fontFamily() const {
    if (!m_fontFamily || m_fontFamily->value().empty())
        return "monospace";
    return m_fontFamily->value();
}

LayoutMode parseLayoutMode(std::string_view value) {
    if (value == "ribbon")
        return LayoutMode::Ribbon;
    if (value == "carousel")
        return LayoutMode::Carousel;
    if (value == "workspace_wall")
        return LayoutMode::WorkspaceWall;

    return LayoutMode::Stage;
}

ShelfMode parseShelfMode(std::string_view value) {
    if (value == "always")
        return ShelfMode::Always;
    if (value == "hidden")
        return ShelfMode::Hidden;
    return ShelfMode::Auto;
}

} // namespace hypr_radiant

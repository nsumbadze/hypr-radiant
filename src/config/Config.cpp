#include <hypr-radiant/config/Config.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace hypr_radiant {

bool RadiantConfig::registerValues(HANDLE handle) {
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

    m_accentColor = makeShared<Config::Values::CStringValue>(
        "plugin:radiant:accent_color",
        "Overview accent color, or auto to follow the active Omarchy theme.",
        "auto");
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

    refreshPalette();

    return HyprlandAPI::addConfigValueV2(handle, m_opacity) && HyprlandAPI::addConfigValueV2(handle, m_animationDurationMs) &&
        HyprlandAPI::addConfigValueV2(handle, m_layout) && HyprlandAPI::addConfigValueV2(handle, m_accentColor) &&
        HyprlandAPI::addConfigValueV2(handle, m_backgroundColor) && HyprlandAPI::addConfigValueV2(handle, m_foregroundColor) &&
        HyprlandAPI::addConfigValueV2(handle, m_fontFamily) &&
        HyprlandAPI::addConfigValueV2(handle, m_gestureEnabled) && HyprlandAPI::addConfigValueV2(handle, m_gestureFingers) &&
        HyprlandAPI::addConfigValueV2(handle, m_gestureDistance) && HyprlandAPI::addConfigValueV2(handle, m_shortcutEnabled);
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

std::optional<CHyprColor> RadiantConfig::accentColorOverride() const {
    if (!m_accentColor)
        return std::nullopt;

    const auto parsed = parseAccentColor(m_accentColor->value());
    if (!parsed)
        return std::nullopt;

    return CHyprColor{parsed->red, parsed->green, parsed->blue, parsed->alpha};
}

void RadiantConfig::refreshPalette() {
    m_palette = loadOmarchyPalette();
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
    if (value == "workspace_wall")
        return LayoutMode::WorkspaceWall;

    return LayoutMode::Stage;
}

} // namespace hypr_radiant

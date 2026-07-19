#include <hypr-radiant/Config.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
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
        "Overview accent color, or auto to inherit the focused Hyprland border.",
        "auto");

    return HyprlandAPI::addConfigValueV2(handle, m_opacity) && HyprlandAPI::addConfigValueV2(handle, m_animationDurationMs) &&
        HyprlandAPI::addConfigValueV2(handle, m_layout) && HyprlandAPI::addConfigValueV2(handle, m_accentColor);
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

LayoutMode parseLayoutMode(std::string_view value) {
    if (value == "workspace_wall")
        return LayoutMode::WorkspaceWall;

    return LayoutMode::Stage;
}

std::optional<RadiantRgba> parseAccentColor(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);

    if (value.empty() || value == "auto")
        return std::nullopt;

    if (value.starts_with('#'))
        value.remove_prefix(1);
    else if (value.starts_with("rgba(") && value.ends_with(')')) {
        value.remove_prefix(5);
        value.remove_suffix(1);
    } else if (value.starts_with("rgb(") && value.ends_with(')')) {
        value.remove_prefix(4);
        value.remove_suffix(1);
    } else if (value.starts_with("0x")) {
        value.remove_prefix(2);
    }

    if (value.size() != 6 && value.size() != 8)
        return std::nullopt;

    std::uint32_t hex = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), hex, 16);
    if (error != std::errc{} || end != value.data() + value.size())
        return std::nullopt;

    if (value.size() == 6)
        hex = (hex << 8U) | 0xFFU;

    constexpr auto channel = [](std::uint32_t number, unsigned shift) {
        return static_cast<float>((number >> shift) & 0xFFU) / 255.0F;
    };
    return RadiantRgba{
        .red   = channel(hex, 24U),
        .green = channel(hex, 16U),
        .blue  = channel(hex, 8U),
        .alpha = channel(hex, 0U),
    };
}

} // namespace hypr_radiant

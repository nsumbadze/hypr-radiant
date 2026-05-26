#include <hypr-radiant/Config.hpp>

#include <algorithm>

namespace hypr_radiant {

bool RadiantConfig::registerValues(HANDLE handle) {
    m_opacity = makeShared<Config::Values::CFloatValue>(
        "plugin:radiant:opacity",
        "Fullscreen overlay opacity.",
        0.55F,
        Config::Values::SFloatValueOptions{.min = 0.0F, .max = 1.0F});

    m_animationDurationMs = makeShared<Config::Values::CIntValue>(
        "plugin:radiant:animation_duration",
        "Overlay fade animation duration in milliseconds.",
        180,
        Config::Values::SIntValueOptions{.min = 0, .max = 2000});

    return HyprlandAPI::addConfigValueV2(handle, m_opacity) && HyprlandAPI::addConfigValueV2(handle, m_animationDurationMs);
}

float RadiantConfig::opacity() const {
    if (!m_opacity)
        return 0.55F;

    return std::clamp(m_opacity->value(), 0.0F, 1.0F);
}

int RadiantConfig::animationDurationMs() const {
    if (!m_animationDurationMs)
        return 180;

    return static_cast<int>(std::clamp(m_animationDurationMs->value(), static_cast<Config::INTEGER>(0), static_cast<Config::INTEGER>(2000)));
}

} // namespace hypr_radiant

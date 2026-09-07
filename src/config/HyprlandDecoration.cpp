#include <hypr-radiant/config/HyprlandDecoration.hpp>

#if HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#endif

namespace hypr_radiant {

class HyprlandDecorationReader::Impl {
  public:
#if HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE
    CConfigValue<Config::INTEGER>             rounding{"decoration:rounding"};
    CConfigValue<Config::INTEGER>             borderSize{"general:border_size"};
    CConfigValue<Config::IComplexConfigValue> activeBorder{"general:col.active_border"};
    CConfigValue<Config::IComplexConfigValue> inactiveBorder{"general:col.inactive_border"};
#endif
};

HyprlandDecorationReader::HyprlandDecorationReader() : m_impl(std::make_unique<Impl>()) {}
HyprlandDecorationReader::~HyprlandDecorationReader() = default;

#if HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE
namespace {

std::optional<BorderGradient> readGradient(const CConfigValue<Config::IComplexConfigValue>& value) {
    if (!value.good())
        return std::nullopt;
    auto* complex = value.ptr();
    if (!complex || complex->getDataType() != Config::CVD_TYPE_GRADIENT)
        return std::nullopt;
    const auto* gradient = static_cast<const Config::CGradientValueData*>(complex);
    if (gradient->m_colors.empty())
        return std::nullopt;

    BorderGradient result;
    result.angle = gradient->m_angle;
    result.stops.reserve(gradient->m_colors.size());
    for (const auto& color : gradient->m_colors) {
        result.stops.push_back({
            .red   = static_cast<float>(color.r),
            .green = static_cast<float>(color.g),
            .blue  = static_cast<float>(color.b),
            .alpha = static_cast<float>(color.a),
        });
    }
    return result;
}

} // namespace
#endif

std::optional<NativeDecoration> HyprlandDecorationReader::read() const {
#if HYPR_RADIANT_HYPRLAND_CONFIG_VALUE_BASE
    if (!m_impl || !m_impl->rounding.good() || !m_impl->borderSize.good())
        return std::nullopt;
    const auto active = readGradient(m_impl->activeBorder);
    const auto inactive = readGradient(m_impl->inactiveBorder);
    if (!active || !inactive)
        return std::nullopt;
    return NativeDecoration{
        .rounding = static_cast<int>(*m_impl->rounding),
        .borderSize = static_cast<int>(*m_impl->borderSize),
        .activeBorder = *active,
        .inactiveBorder = *inactive,
    };
#else
    return std::nullopt;
#endif
}

} // namespace hypr_radiant

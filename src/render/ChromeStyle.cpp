#include <hypr-radiant/render/ChromeStyle.hpp>

#include <algorithm>

namespace hypr_radiant {

int ChromeStyle::radius(int radiantRadius, int outset) const {
    if (!rounding)
        return std::max(0, radiantRadius + outset);
    if (*rounding == 0)
        return 0;
    return std::max(0, *rounding + outset);
}

int ChromeStyle::borderWidth(int radiantWidth) const {
    return borderSize.value_or(radiantWidth);
}

bool ChromeStyle::usesRadiantGradient() const {
    return preset == ChromePreset::Radiant && !selectedBorder;
}

std::string chromeStyleDescription(const ChromeStyle& style) {
    std::string description = style.preset == ChromePreset::Native ? "Desktop borders" :
        style.preset == ChromePreset::Flat ? "Fixed style" : "Radiant style";
    if (style.nativeUnavailable)
        description = "Desktop unavailable; Square fallback";
    description += ": ";
    description += !style.rounding ? "rounded cards" : *style.rounding == 0 ? "square corners" : std::to_string(*style.rounding) + "px corners";
    description += style.borderSize ? ", " + std::to_string(*style.borderSize) + "px borders" : ", accent borders";
    description += style.effects ? "; effects on" : "; effects off";
    return description;
}

ChromePreset parseChromePreset(std::string_view value) {
    if (value == "native")
        return ChromePreset::Native;
    if (value == "flat")
        return ChromePreset::Flat;
    return ChromePreset::Radiant;
}

EffectsMode parseEffectsMode(std::string_view value) {
    if (value == "on")
        return EffectsMode::On;
    if (value == "off")
        return EffectsMode::Off;
    return EffectsMode::Auto;
}

ChromeStyle resolveChromeStyle(const ChromeInputs& inputs) {
    ChromeStyle style;
    style.preset = inputs.preset;
    if (inputs.preset == ChromePreset::Flat) {
        style.rounding = 0;
        style.borderSize = 2;
        style.effects = false;
    } else if (inputs.preset == ChromePreset::Native) {
        style.effects = false;
        if (inputs.native) {
            style.rounding = std::clamp(inputs.native->rounding, 0, 40);
            style.borderSize = std::clamp(inputs.native->borderSize, 0, 12);
            if (!inputs.native->activeBorder.stops.empty())
                style.selectedBorder = inputs.native->activeBorder;
            if (!inputs.native->inactiveBorder.stops.empty())
                style.inactiveBorder = inputs.native->inactiveBorder;
        } else {
            style.rounding = 0;
            style.borderSize = 2;
            style.nativeUnavailable = true;
        }
    }

    if (inputs.roundingOverride >= 0)
        style.rounding = std::clamp(inputs.roundingOverride, 0, 40);
    if (inputs.borderSizeOverride >= 0)
        style.borderSize = std::clamp(inputs.borderSizeOverride, 0, 12);
    if (inputs.borderColorOverride)
        style.selectedBorder = BorderGradient{.stops = {*inputs.borderColorOverride}};
    if (inputs.effects == EffectsMode::On)
        style.effects = true;
    else if (inputs.effects == EffectsMode::Off)
        style.effects = false;
    return style;
}

} // namespace hypr_radiant

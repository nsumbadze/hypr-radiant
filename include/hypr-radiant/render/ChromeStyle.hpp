#pragma once

#include <hypr-radiant/config/Color.hpp>

#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace hypr_radiant {

enum class ChromePreset { Radiant,
    Native,
    Flat };
enum class EffectsMode { Auto,
    On,
    Off };

struct BorderGradient {
    std::vector<RadiantRgba> stops;
    float angle = 0.F;
};

struct NativeDecoration {
    int            rounding   = 0;
    int            borderSize = 2;
    BorderGradient activeBorder;
    BorderGradient inactiveBorder;
};

struct ChromeInputs {
    ChromePreset preset  = ChromePreset::Radiant;
    int                             roundingOverride = -1;
    int                             borderSizeOverride = -1;
    std::optional<RadiantRgba>      borderColorOverride;
    EffectsMode  effects = EffectsMode::Auto;
    std::optional<NativeDecoration> native;
};

struct ChromeStyle {
    ChromePreset preset = ChromePreset::Radiant;
    std::optional<int>            rounding;
    std::optional<int>            borderSize;
    std::optional<BorderGradient> selectedBorder;
    std::optional<BorderGradient> inactiveBorder;
    bool                          effects = true;
    bool                          nativeUnavailable = false;

    [[nodiscard]] int radius(int radiantRadius, int outset = 0) const;
    [[nodiscard]] int borderWidth(int radiantWidth) const;
    [[nodiscard]] bool usesRadiantGradient() const;
};

[[nodiscard]] ChromePreset parseChromePreset(std::string_view value);
[[nodiscard]] EffectsMode  parseEffectsMode(std::string_view value);
[[nodiscard]] ChromeStyle  resolveChromeStyle(const ChromeInputs& inputs);
[[nodiscard]] std::string chromeStyleDescription(const ChromeStyle& style);

} // namespace hypr_radiant

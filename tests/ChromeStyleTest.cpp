#include <hypr-radiant/render/ChromeStyle.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

BorderGradient gradient(float red, float angle = 0.F) {
    return {.stops = {{.red = red, .green = 0.2F, .blue = 0.3F, .alpha = 1.F}}, .angle = angle};
}

void radiantPassesThroughLiterals() {
    const auto style = resolveChromeStyle({});
    assert(style.preset == ChromePreset::Radiant);
    assert(style.radius(14) == 14);
    assert(style.radius(18, 16) == 34);
    assert(style.borderWidth(1) == 1);
    assert(style.effects);
}

void flatIsSquareThickAndEffectless() {
    ChromeInputs inputs;
    inputs.preset = ChromePreset::Flat;
    const auto style = resolveChromeStyle(inputs);
    assert(style.radius(14, 16) == 0);
    assert(style.borderWidth(1) == 2);
    assert(!style.effects);
}

void nativeMirrorsDecorationAndFallsBackSafely() {
    ChromeInputs inputs;
    inputs.preset = ChromePreset::Native;
    inputs.native = NativeDecoration{.rounding = 9, .borderSize = 3, .activeBorder = gradient(0.8F, 1.2F), .inactiveBorder = gradient(0.2F)};
    const auto native = resolveChromeStyle(inputs);
    assert(native.radius(18) == 9);
    assert(native.borderWidth(1) == 3);
    assert(native.selectedBorder->angle == 1.2F);
    assert(native.inactiveBorder.has_value());
    assert(!native.nativeUnavailable);

    inputs.native.reset();
    const auto missing = resolveChromeStyle(inputs);
    assert(missing.radius(14) == 0);
    assert(missing.borderWidth(1) == 2);
    assert(missing.nativeUnavailable);
}

void overridesWinAndClamp() {
    ChromeInputs inputs;
    inputs.preset = ChromePreset::Flat;
    inputs.roundingOverride = 80;
    inputs.borderSizeOverride = 20;
    inputs.borderColorOverride = RadiantRgba{.red = 1.F};
    inputs.effects = EffectsMode::On;
    const auto style = resolveChromeStyle(inputs);
    assert(style.radius(2) == 40);
    assert(style.borderWidth(1) == 12);
    assert(style.selectedBorder->stops.front().red == 1.F);
    assert(style.effects);
}

void parsersFallBackToDefaults() {
    assert(parseChromePreset("native") == ChromePreset::Native);
    assert(parseChromePreset("flat") == ChromePreset::Flat);
    assert(parseChromePreset("unknown") == ChromePreset::Radiant);
    assert(parseEffectsMode("on") == EffectsMode::On);
    assert(parseEffectsMode("off") == EffectsMode::Off);
    assert(parseEffectsMode("unknown") == EffectsMode::Auto);
}

} // namespace

int main() {
    radiantPassesThroughLiterals();
    flatIsSquareThickAndEffectless();
    nativeMirrorsDecorationAndFallsBackSafely();
    overridesWinAndClamp();
    parsersFallBackToDefaults();
    std::cout << "ChromeStyleTest passed\n";
}

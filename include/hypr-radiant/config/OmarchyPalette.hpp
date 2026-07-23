#pragma once

#include <hypr-radiant/config/Color.hpp>

#include <string>
#include <string_view>

namespace hypr_radiant {

/// Palette sourced from the active Omarchy theme's `colors.toml`.
///
/// Defaults are a neutral gray so the overview stays legible on systems without Omarchy
/// installed, or when the theme file is missing or unreadable.
struct OmarchyPalette {
    RadiantRgba background{.red = 0.129F, .green = 0.137F, .blue = 0.145F, .alpha = 1.0F};
    RadiantRgba foreground{.red = 0.847F, .green = 0.855F, .blue = 0.867F, .alpha = 1.0F};
    RadiantRgba accent{.red = 0.482F, .green = 0.522F, .blue = 0.557F, .alpha = 1.0F};
    bool      loaded = false;
};

/// Parses the `key = "#RRGGBB"` pairs of an Omarchy `colors.toml` payload.
/// Unrecognised or malformed keys keep their neutral gray default.
[[nodiscard]] OmarchyPalette parseOmarchyPalette(std::string_view contents);

/// Path consulted by loadOmarchyPalette(). Empty when `$HOME` is unset.
[[nodiscard]] std::string omarchyPalettePath();

/// Reads the active theme palette, falling back to neutral gray when unavailable.
[[nodiscard]] OmarchyPalette loadOmarchyPalette();

/// True when the palette reads as a light theme, so surfaces must darken rather than lighten.
[[nodiscard]] bool isLightPalette(const OmarchyPalette& palette);

/// Shifts `base` toward the palette's contrasting end by `amount` (0..1).
/// Lightens on dark themes and darkens on light ones, so one set of call sites serves both.
[[nodiscard]] RadiantRgba liftedSurface(const OmarchyPalette& palette, const RadiantRgba& base, float amount);

} // namespace hypr_radiant

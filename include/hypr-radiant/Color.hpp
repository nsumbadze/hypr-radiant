#pragma once

#include <optional>
#include <string_view>

namespace hypr_radiant {

struct RadiantRgba {
    float red   = 0.0F;
    float green = 0.0F;
    float blue  = 0.0F;
    float alpha = 1.0F;
};

/// Parses `#RRGGBB`, `#RRGGBBAA`, `rgb(...)`, `rgba(...)`, and `0x...` forms.
/// Returns nullopt for empty input, `auto`, and anything malformed.
[[nodiscard]] std::optional<RadiantRgba> parseAccentColor(std::string_view value);

/// Perceptual luminance in [0, 1], used to decide whether a palette reads as light or dark.
[[nodiscard]] float relativeLuminance(const RadiantRgba& color);

} // namespace hypr_radiant

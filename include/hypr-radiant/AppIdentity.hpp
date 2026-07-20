#pragma once

#include <string>
#include <string_view>

namespace hypr_radiant {

struct AppSignalColor {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

[[nodiscard]] std::string appGlyph(std::string_view appClass);
[[nodiscard]] AppSignalColor appSignalColor(std::string_view appClass, AppSignalColor inheritedAccent);

} // namespace hypr_radiant

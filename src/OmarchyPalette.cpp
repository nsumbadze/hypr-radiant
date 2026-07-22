#include <hypr-radiant/OmarchyPalette.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hypr_radiant {
namespace {

constexpr std::string_view PALETTE_RELATIVE_PATH = "/.config/omarchy/current/theme/colors.toml";

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

std::string_view unquote(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return value;
}

} // namespace

OmarchyPalette parseOmarchyPalette(std::string_view contents) {
    OmarchyPalette palette;

    while (!contents.empty()) {
        const auto lineEnd = contents.find('\n');
        const auto line    = trim(contents.substr(0, lineEnd));
        contents           = lineEnd == std::string_view::npos ? std::string_view{} : contents.substr(lineEnd + 1);

        if (line.empty() || line.front() == '#')
            continue;

        const auto separator = line.find('=');
        if (separator == std::string_view::npos)
            continue;

        const auto key    = trim(line.substr(0, separator));
        const auto parsed = parseAccentColor(unquote(trim(line.substr(separator + 1))));
        if (!parsed)
            continue;

        if (key == "background") {
            palette.background = *parsed;
            palette.loaded     = true;
        } else if (key == "foreground") {
            palette.foreground = *parsed;
            palette.loaded     = true;
        } else if (key == "accent") {
            palette.accent = *parsed;
            palette.loaded = true;
        }
    }

    return palette;
}

std::string omarchyPalettePath() {
    const auto* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0')
        return {};

    return std::string{home} + std::string{PALETTE_RELATIVE_PATH};
}

OmarchyPalette loadOmarchyPalette() {
    const auto path = omarchyPalettePath();
    if (path.empty())
        return {};

    std::ifstream file{path};
    if (!file)
        return {};

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseOmarchyPalette(buffer.str());
}

bool isLightPalette(const OmarchyPalette& palette) {
    return relativeLuminance(palette.background) > 0.5F;
}

RadiantRgba liftedSurface(const OmarchyPalette& palette, const RadiantRgba& base, float amount) {
    // Light themes need surfaces to step darker; dark themes need them lighter. Targeting the
    // opposite end of the range keeps every call site direction-agnostic.
    const auto target = isLightPalette(palette) ? 0.0F : 1.0F;
    const auto mix    = std::clamp(amount, 0.0F, 1.0F);
    const auto blend  = [target, mix](float channel) {
        return channel + (target - channel) * mix;
    };

    return RadiantRgba{
        .red   = blend(base.red),
        .green = blend(base.green),
        .blue  = blend(base.blue),
        .alpha = base.alpha,
    };
}

} // namespace hypr_radiant

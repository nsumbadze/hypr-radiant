#include <hypr-radiant/AppIdentity.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>

namespace hypr_radiant {
namespace {

std::string normalizedAppClass(std::string_view appClass) {
    std::string normalized{appClass};
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return normalized;
}

bool classContains(const std::string& appClass, std::string_view candidate) {
    return appClass.find(candidate) != std::string::npos;
}

AppSignalColor inherit(AppSignalColor color, AppSignalColor accent, float amount) {
    color.r = std::lerp(color.r, accent.r, amount);
    color.g = std::lerp(color.g, accent.g, amount);
    color.b = std::lerp(color.b, accent.b, amount);
    color.a = 1.0F;
    return color;
}

} // namespace

std::string appGlyph(std::string_view appClass) {
    const auto normalized = normalizedAppClass(appClass);
    if (classContains(normalized, "firefox") || classContains(normalized, "zen"))
        return "󰈹";
    if (classContains(normalized, "chrom") || classContains(normalized, "browser"))
        return "󰊯";
    if (classContains(normalized, "code") || classContains(normalized, "editor") || classContains(normalized, "opencode"))
        return "󰨞";
    if (classContains(normalized, "foot") || classContains(normalized, "kitty") || classContains(normalized, "ghostty") ||
        classContains(normalized, "alacritty") || classContains(normalized, "term"))
        return "";
    if (classContains(normalized, "discord") || classContains(normalized, "slack") || classContains(normalized, "chat"))
        return "󰙯";
    if (classContains(normalized, "nautilus") || classContains(normalized, "thunar") || classContains(normalized, "file"))
        return "󰉋";
    if (classContains(normalized, "spotify") || classContains(normalized, "music"))
        return "󰎆";
    return "󰣆";
}

AppSignalColor appSignalColor(std::string_view appClass, AppSignalColor inheritedAccent) {
    const auto normalized = normalizedAppClass(appClass);

    if (classContains(normalized, "firefox") || classContains(normalized, "zen"))
        return inherit({1.0F, 0.43F, 0.20F, 1.0F}, inheritedAccent, 0.12F);
    if (classContains(normalized, "chrom") || classContains(normalized, "browser"))
        return inherit({0.28F, 0.57F, 0.96F, 1.0F}, inheritedAccent, 0.12F);
    if (classContains(normalized, "code") || classContains(normalized, "editor") || classContains(normalized, "opencode"))
        return inherit({0.12F, 0.68F, 0.96F, 1.0F}, inheritedAccent, 0.16F);
    if (classContains(normalized, "foot") || classContains(normalized, "kitty") || classContains(normalized, "ghostty") ||
        classContains(normalized, "alacritty") || classContains(normalized, "term")) {
        inheritedAccent.a = 1.0F;
        return inheritedAccent;
    }
    if (classContains(normalized, "discord") || classContains(normalized, "slack") || classContains(normalized, "chat"))
        return inherit({0.42F, 0.45F, 0.96F, 1.0F}, inheritedAccent, 0.15F);
    if (classContains(normalized, "nautilus") || classContains(normalized, "thunar") || classContains(normalized, "file"))
        return inherit({0.83F, 0.78F, 0.52F, 1.0F}, inheritedAccent, 0.18F);
    if (classContains(normalized, "spotify") || classContains(normalized, "music"))
        return inherit({0.16F, 0.82F, 0.38F, 1.0F}, inheritedAccent, 0.14F);

    static constexpr std::array<AppSignalColor, 5> palette{
        AppSignalColor{0.38F, 0.74F, 0.98F, 1.0F},
        AppSignalColor{0.68F, 0.55F, 0.98F, 1.0F},
        AppSignalColor{0.31F, 0.84F, 0.70F, 1.0F},
        AppSignalColor{0.95F, 0.62F, 0.34F, 1.0F},
        AppSignalColor{0.93F, 0.47F, 0.66F, 1.0F},
    };
    std::uint32_t hash = 2166136261U;
    for (const auto character : normalized) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 16777619U;
    }
    return inherit(palette[hash % palette.size()], inheritedAccent, 0.22F);
}

} // namespace hypr_radiant

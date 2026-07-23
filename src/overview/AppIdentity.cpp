#include <hypr-radiant/overview/AppIdentity.hpp>

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

} // namespace hypr_radiant

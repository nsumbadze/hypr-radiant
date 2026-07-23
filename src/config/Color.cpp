#include <hypr-radiant/config/Color.hpp>

#include <cctype>
#include <charconv>
#include <cstdint>

namespace hypr_radiant {

std::optional<RadiantRgba> parseAccentColor(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);

    if (value.empty() || value == "auto")
        return std::nullopt;

    if (value.starts_with('#'))
        value.remove_prefix(1);
    else if (value.starts_with("rgba(") && value.ends_with(')')) {
        value.remove_prefix(5);
        value.remove_suffix(1);
    } else if (value.starts_with("rgb(") && value.ends_with(')')) {
        value.remove_prefix(4);
        value.remove_suffix(1);
    } else if (value.starts_with("0x")) {
        value.remove_prefix(2);
    }

    if (value.size() != 6 && value.size() != 8)
        return std::nullopt;

    std::uint32_t hex = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), hex, 16);
    if (error != std::errc{} || end != value.data() + value.size())
        return std::nullopt;

    if (value.size() == 6)
        hex = (hex << 8U) | 0xFFU;

    constexpr auto channel = [](std::uint32_t number, unsigned shift) {
        return static_cast<float>((number >> shift) & 0xFFU) / 255.0F;
    };
    return RadiantRgba{
        .red   = channel(hex, 24U),
        .green = channel(hex, 16U),
        .blue  = channel(hex, 8U),
        .alpha = channel(hex, 0U),
    };
}

float relativeLuminance(const RadiantRgba& color) {
    return 0.2126F * color.red + 0.7152F * color.green + 0.0722F * color.blue;
}

} // namespace hypr_radiant

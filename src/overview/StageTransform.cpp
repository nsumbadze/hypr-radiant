#include <hypr-radiant/overview/StageTransform.hpp>

#include <algorithm>

namespace hypr_radiant {
namespace {

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
}

bool contains(const LayoutRect& rect, RadiantPoint point) {
    return rect.width > 0.0 && rect.height > 0.0 && point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
        point.y < rect.y + rect.height;
}

} // namespace

LayoutRect remapStageRect(const LayoutRect& child, const LayoutRect& source, const LayoutRect& target) {
    if (source.width <= 0.0 || source.height <= 0.0 || target.width <= 0.0 || target.height <= 0.0)
        return child;

    const auto scale = std::min(target.width / source.width, target.height / source.height);
    const auto contentWidth = source.width * scale;
    const auto contentHeight = source.height * scale;
    const auto originX = target.x + centered(target.width, contentWidth);
    const auto originY = target.y + centered(target.height, contentHeight);
    return {
        .x = originX + (child.x - source.x) * scale,
        .y = originY + (child.y - source.y) * scale,
        .width = child.width * scale,
        .height = child.height * scale,
    };
}

std::optional<RadiantPoint> mapStagePointToSource(const LayoutRect& source, const LayoutRect& target, RadiantPoint point) {
    if (source.width <= 0.0 || source.height <= 0.0 || target.width <= 0.0 || target.height <= 0.0)
        return std::nullopt;

    const auto scale = std::min(target.width / source.width, target.height / source.height);
    const auto contentWidth = source.width * scale;
    const auto contentHeight = source.height * scale;
    const auto originX = target.x + centered(target.width, contentWidth);
    const auto originY = target.y + centered(target.height, contentHeight);
    const LayoutRect content{.x = originX, .y = originY, .width = contentWidth, .height = contentHeight};
    if (!contains(content, point))
        return std::nullopt;

    return RadiantPoint{
        .x = source.x + (point.x - originX) / scale,
        .y = source.y + (point.y - originY) / scale,
    };
}

} // namespace hypr_radiant

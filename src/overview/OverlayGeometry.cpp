#include <hypr-radiant/overview/OverlayGeometry.hpp>

#include <hypr-radiant/overview/StageTransform.hpp>

#include <algorithm>
#include <cmath>

namespace hypr_radiant {

double centered(double available, double size) {
    return std::max(0.0, (available - size) / 2.0);
}

bool contains(const LayoutRect& rect, double x, double y) {
    return rect.width > 0.0 && rect.height > 0.0 && x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

bool intersects(const LayoutRect& lhs, const LayoutRect& rhs) {
    return lhs.width > 0.0 && lhs.height > 0.0 && rhs.width > 0.0 && rhs.height > 0.0 && lhs.x < rhs.x + rhs.width &&
        lhs.x + lhs.width > rhs.x && lhs.y < rhs.y + rhs.height && lhs.y + lhs.height > rhs.y;
}

LayoutRect scaledAroundCenter(const LayoutRect& rect, double scale, double offsetY) {
    const auto width  = rect.width * scale;
    const auto height = rect.height * scale;
    return {
        .x      = rect.x - (width - rect.width) / 2.0,
        .y      = rect.y - (height - rect.height) / 2.0 + offsetY,
        .width  = width,
        .height = height,
    };
}

LayoutRect interpolatedRect(const LayoutRect& from, const LayoutRect& to, double progress) {
    return {
        .x      = std::lerp(from.x, to.x, progress),
        .y      = std::lerp(from.y, to.y, progress),
        .width  = std::lerp(from.width, to.width, progress),
        .height = std::lerp(from.height, to.height, progress),
    };
}

LayoutRect remapRect(const LayoutRect& child, const LayoutRect& source, const LayoutRect& target) {
    if (source.width <= 0.0 || source.height <= 0.0)
        return child;

    return {
        .x      = target.x + (child.x - source.x) * target.width / source.width,
        .y      = target.y + (child.y - source.y) * target.height / source.height,
        .width  = child.width * target.width / source.width,
        .height = child.height * target.height / source.height,
    };
}

LayoutRect collapsedStageBounds(const WorkspaceWallFrame& frame) {
    const auto bottom     = frame.stage.bounds.y + frame.stage.bounds.height;
    const auto collapsedY = std::min(bottom, frame.rail.bounds.y + 70.0);
    const auto sideInset  = std::min(24.0, frame.bounds.width * 0.025);
    return {
        .x      = sideInset,
        .y      = collapsedY,
        .width  = std::max(0.0, frame.bounds.width - sideInset * 2.0),
        .height = std::max(0.0, bottom - collapsedY),
    };
}

RadiantPoint mapDisplayedStagePoint(const WorkspaceWallFrame& frame, RadiantPoint point, bool shelfVisible) {
    if (shelfVisible)
        return point;

    const auto displayed = collapsedStageBounds(frame);
    return mapStagePointToSource(frame.stage.bounds, displayed, point).value_or(point);
}

} // namespace hypr_radiant

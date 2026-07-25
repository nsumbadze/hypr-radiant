#pragma once

#include <hypr-radiant/RadiantState.hpp>
#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

namespace hypr_radiant {

/// Largest offset that still keeps `size` centred in `available`, never negative.
[[nodiscard]] double centered(double available, double size);

/// Half-open point-in-rect test; false for zero-area rects.
[[nodiscard]] bool contains(const LayoutRect& rect, double x, double y);

/// Whether two rects overlap; false if either is zero-area.
[[nodiscard]] bool intersects(const LayoutRect& lhs, const LayoutRect& rhs);

/// Scales a rect about its own centre, optionally nudged down by `offsetY`.
[[nodiscard]] LayoutRect scaledAroundCenter(const LayoutRect& rect, double scale, double offsetY = 0.0);

/// Shrinks a closing window toward its centre while letting it settle slightly downward.
/// `progress` is 1 for the untouched card and 0 for the fully dismissed card.
[[nodiscard]] LayoutRect windowDismissalRect(const LayoutRect& rect, double progress);

/// Per-component linear interpolation between two rects.
[[nodiscard]] LayoutRect interpolatedRect(const LayoutRect& from, const LayoutRect& to, double progress);

/// Maps `child` from the `source` box into the `target` box, stretching independently per axis.
/// Returns `child` unchanged when `source` has no area.
[[nodiscard]] LayoutRect remapRect(const LayoutRect& child, const LayoutRect& source, const LayoutRect& target);

/// The shrunken stage rectangle shown while the workspace shelf is retracted.
[[nodiscard]] LayoutRect collapsedStageBounds(const WorkspaceWallFrame& frame);

/// Maps a pointer position on the displayed (possibly collapsed) stage back to the source stage
/// coordinates the hit test expects. A visible shelf means no collapse, so the point passes through.
[[nodiscard]] RadiantPoint mapDisplayedStagePoint(const WorkspaceWallFrame& frame, RadiantPoint point, bool shelfVisible);

} // namespace hypr_radiant

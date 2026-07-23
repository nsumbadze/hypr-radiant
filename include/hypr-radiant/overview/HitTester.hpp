#pragma once

#include <hypr-radiant/OverviewTarget.hpp>
#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

namespace hypr_radiant {

[[nodiscard]] RadiantPoint mapGlobalPointToFrame(
    const LayoutRect& globalBounds,
    const LayoutRect& frameBounds,
    double globalX,
    double globalY) noexcept;

/// Hotspot for the close affordance in a stage window card's top-right corner. Returns an empty
/// rect when the card is too small to carry one without burying the preview underneath it.
/// Hit testing and rendering both derive the button from this, so they cannot drift apart.
[[nodiscard]] LayoutRect closeButtonRect(const LayoutRect& windowRect) noexcept;

class HitTester {
  public:
    [[nodiscard]] OverviewTarget hitTest(const WorkspaceWallFrame& frame, double x, double y) const;
    [[nodiscard]] OverviewTarget initialSelection(const WorkspaceWallFrame& frame) const;
    [[nodiscard]] OverviewTarget moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction) const;
};

} // namespace hypr_radiant

#pragma once

#include <hypr-radiant/OverviewTarget.hpp>
#include <hypr-radiant/overview/WorkspaceWallLayout.hpp>

namespace hypr_radiant {

struct NavigationOptions {
    bool spatialWindows = false;
    bool allShelfTargets = false;
    OverviewTarget returnWindow;
};

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
    /// Hit-tests the Stage view at its rendered shelf progress. Unlike hitTest(), this accounts for
    /// the rail sliding in and the stage expanding underneath it.
    [[nodiscard]] OverviewTarget hitTestDisplayedStage(
        const WorkspaceWallFrame& frame, double x, double y, double shelfProgress) const;
    [[nodiscard]] OverviewTarget initialSelection(const WorkspaceWallFrame& frame) const;
    [[nodiscard]] OverviewTarget moveSelection(const WorkspaceWallFrame& frame, OverviewTarget current, NavigationDirection direction,
        NavigationOptions options = {}) const;
    [[nodiscard]] OverviewTarget cycleWindow(const WorkspaceWallFrame& frame, OverviewTarget current, int step) const;
};

} // namespace hypr_radiant

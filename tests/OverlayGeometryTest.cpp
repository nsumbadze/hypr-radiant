#include <hypr-radiant/overview/OverlayGeometry.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

using namespace hypr_radiant;

namespace {

bool near(double lhs, double rhs, double eps = 0.001) {
    return std::abs(lhs - rhs) < eps;
}

WorkspaceWallFrame stageFrame() {
    WorkspaceWallFrame frame;
    frame.bounds      = {.x = 0.0, .y = 0.0, .width = 1920.0, .height = 1080.0};
    frame.rail.bounds = {.x = 40.0, .y = 24.0, .width = 1840.0, .height = 180.0};
    frame.stage.bounds = {.x = 120.0, .y = 260.0, .width = 1680.0, .height = 720.0};
    return frame;
}

void centeredNeverGoesNegative() {
    assert(near(centered(100.0, 40.0), 30.0));
    assert(near(centered(40.0, 100.0), 0.0)); // content wider than the box clamps to 0, not -30
}

void containsIsHalfOpenAndRejectsZeroArea() {
    const LayoutRect rect{.x = 10.0, .y = 10.0, .width = 100.0, .height = 50.0};
    assert(contains(rect, 10.0, 10.0));    // top-left corner is inside
    assert(!contains(rect, 110.0, 10.0));  // right edge is outside (half-open)
    assert(!contains(rect, 60.0, 60.0));   // bottom edge is outside
    assert(!contains({.x = 0.0, .y = 0.0, .width = 0.0, .height = 50.0}, 0.0, 10.0));
}

void intersectsMatchesOverlap() {
    const LayoutRect a{.x = 0.0, .y = 0.0, .width = 100.0, .height = 100.0};
    assert(intersects(a, {.x = 50.0, .y = 50.0, .width = 100.0, .height = 100.0}));
    assert(!intersects(a, {.x = 100.0, .y = 0.0, .width = 10.0, .height = 10.0})); // touching edges do not overlap
    assert(!intersects(a, {.x = 10.0, .y = 10.0, .width = 0.0, .height = 10.0}));  // zero area never intersects
}

void scaleKeepsTheCenterFixed() {
    const LayoutRect rect{.x = 100.0, .y = 200.0, .width = 80.0, .height = 40.0};
    const auto scaled = scaledAroundCenter(rect, 1.5);

    const auto cx = rect.x + rect.width / 2.0;
    const auto cy = rect.y + rect.height / 2.0;
    assert(near(scaled.x + scaled.width / 2.0, cx));
    assert(near(scaled.y + scaled.height / 2.0, cy));
    assert(near(scaled.width, 120.0));

    const auto nudged = scaledAroundCenter(rect, 1.0, -6.0);
    assert(near(nudged.y, rect.y - 6.0));
    assert(near(nudged.width, rect.width)); // scale 1.0 leaves size untouched
}

void windowDismissalShrinksAndSettles() {
    const LayoutRect rect{.x = 100.0, .y = 200.0, .width = 200.0, .height = 100.0};
    const auto open   = windowDismissalRect(rect, 1.0);
    const auto midway = windowDismissalRect(rect, 0.5);
    const auto closed = windowDismissalRect(rect, 0.0);

    assert(near(open.x, rect.x) && near(open.y, rect.y));
    assert(near(open.width, rect.width) && near(open.height, rect.height));
    assert(midway.width < open.width && midway.width > closed.width);
    assert(midway.y > open.y);
    assert(near(closed.width, rect.width * 0.84));
    assert(near(closed.height, rect.height * 0.84));
    assert(near(closed.y + closed.height / 2.0, rect.y + rect.height / 2.0 + 5.0));

    const auto clamped = windowDismissalRect(rect, -10.0);
    assert(near(clamped.x, closed.x) && near(clamped.width, closed.width));
}

void interpolationHitsBothEnds() {
    const LayoutRect from{.x = 0.0, .y = 0.0, .width = 10.0, .height = 10.0};
    const LayoutRect to{.x = 100.0, .y = 50.0, .width = 30.0, .height = 20.0};
    const auto start = interpolatedRect(from, to, 0.0);
    const auto end   = interpolatedRect(from, to, 1.0);
    const auto mid   = interpolatedRect(from, to, 0.5);
    assert(near(start.x, 0.0) && near(start.width, 10.0));
    assert(near(end.x, 100.0) && near(end.height, 20.0));
    assert(near(mid.x, 50.0) && near(mid.width, 20.0));
}

void remapRectRoundTripsThroughItsInverse() {
    const LayoutRect source{.x = 0.0, .y = 0.0, .width = 200.0, .height = 100.0};
    const LayoutRect target{.x = 500.0, .y = 300.0, .width = 400.0, .height = 400.0};
    const LayoutRect child{.x = 50.0, .y = 25.0, .width = 20.0, .height = 10.0};

    const auto mapped = remapRect(child, source, target);
    const auto back   = remapRect(mapped, target, source);
    assert(near(back.x, child.x) && near(back.y, child.y));
    assert(near(back.width, child.width) && near(back.height, child.height));

    // A zero-area source is a no-op rather than a divide-by-zero.
    const auto safe = remapRect(child, {.x = 0.0, .y = 0.0, .width = 0.0, .height = 0.0}, target);
    assert(near(safe.x, child.x) && near(safe.width, child.width));
}

void collapsedStageFillsTheSpaceTheShelfVacates() {
    const auto frame     = stageFrame();
    const auto collapsed = collapsedStageBounds(frame);

    // "Collapsed" is the shelf, not the stage: with the shelf hidden the stage grows up toward the
    // rail. So the top moves above the normal stage top while the bottom is unchanged, and the whole
    // rect stays inset within the frame.
    assert(collapsed.x >= 0.0);
    assert(collapsed.x + collapsed.width <= frame.bounds.width + 0.001);
    assert(collapsed.y <= frame.stage.bounds.y);                                                  // extends upward
    assert(near(collapsed.y + collapsed.height, frame.stage.bounds.y + frame.stage.bounds.height)); // bottom pinned
    assert(collapsed.height > frame.stage.bounds.height);                                          // larger, not smaller
}

void displayedPointRoundTripsToSourceStage() {
    const auto frame = stageFrame();

    // With the shelf up there is no collapse, so the point is returned untouched.
    const auto passthrough = mapDisplayedStagePoint(frame, {.x = 640.0, .y = 500.0}, true);
    assert(near(passthrough.x, 640.0) && near(passthrough.y, 500.0));

    // With the shelf retracted, the centre of the collapsed stage must map back to the centre of the
    // real stage — this is the mapping a click goes through when the shelf is hidden (the default),
    // so an error here lands clicks on the wrong window.
    const auto collapsed = collapsedStageBounds(frame);
    const RadiantPoint collapsedCenter{.x = collapsed.x + collapsed.width / 2.0, .y = collapsed.y + collapsed.height / 2.0};
    const auto mapped = mapDisplayedStagePoint(frame, collapsedCenter, false);

    const auto stageCx = frame.stage.bounds.x + frame.stage.bounds.width / 2.0;
    const auto stageCy = frame.stage.bounds.y + frame.stage.bounds.height / 2.0;
    assert(near(mapped.x, stageCx, 0.5));
    assert(near(mapped.y, stageCy, 0.5));

    // The map is aspect-preserving, so the centre is the fixed point; a displayed point right of
    // centre still maps right of the stage centre (monotonic), which is all a click needs.
    const RadiantPoint rightOfCenter{.x = collapsedCenter.x + 200.0, .y = collapsedCenter.y};
    const auto mappedRight = mapDisplayedStagePoint(frame, rightOfCenter, false);
    assert(mappedRight.x > stageCx);
}

void dragCardLiftsFromItsSlotToThePointer() {
    const LayoutRect slot{.x = 400.0, .y = 300.0, .width = 260.0, .height = 150.0};
    const RadiantPoint pointer{.x = 900.0, .y = 700.0};

    // Progress 0 is the untouched card: the pick-up starts where the window already is, so the
    // first frame of a drag cannot jump.
    const auto resting = dragCardRect(slot, pointer, 0.0);
    assert(near(resting.x, slot.x) && near(resting.y, slot.y));
    assert(near(resting.width, slot.width) && near(resting.height, slot.height));

    // Progress 1 is centred on the pointer, which is what the drop hit test uses.
    const auto lifted = dragCardRect(slot, pointer, 1.0);
    assert(near(lifted.x + lifted.width / 2.0, pointer.x));
    assert(near(lifted.y + lifted.height / 2.0, pointer.y));

    // Mid-flight sits between the two, and out-of-range progress clamps instead of overshooting.
    const auto half = dragCardRect(slot, pointer, 0.5);
    assert(half.x > slot.x && half.x < lifted.x);
    const auto overshoot = dragCardRect(slot, pointer, 4.0);
    assert(near(overshoot.x, lifted.x) && near(overshoot.width, lifted.width));

    // A tiny wall card is magnified to the floor size rather than dragged as a speck.
    const auto tiny = dragCardRect({.x = 0.0, .y = 0.0, .width = 90.0, .height = 60.0}, pointer, 1.0);
    assert(near(tiny.width, 180.0));
}

void dragLandingCentresInsideTheWorkspaceCard() {
    const LayoutRect card{.x = 800.0, .y = 600.0, .width = 300.0, .height = 180.0};
    const LayoutRect workspace{.x = 100.0, .y = 100.0, .width = 480.0, .height = 300.0};

    const auto landing = dragLandingRect(card, workspace);
    assert(near(landing.x + landing.width / 2.0, workspace.x + workspace.width / 2.0));
    assert(near(landing.y + landing.height / 2.0, workspace.y + workspace.height / 2.0));

    // Fits well inside the destination and keeps the card's aspect, so the settle never covers the
    // workspace it is landing on.
    assert(landing.width < workspace.width && landing.height < workspace.height);
    assert(near(landing.width / landing.height, card.width / card.height, 0.01));
    assert(landing.width <= card.width); // settling shrinks, never swells

    // Degenerate destinations fall back to the card untouched instead of collapsing it.
    const auto safe = dragLandingRect(card, {.x = 0.0, .y = 0.0, .width = 0.0, .height = 200.0});
    assert(near(safe.width, card.width) && near(safe.x, card.x));
}

} // namespace

int main() {
    centeredNeverGoesNegative();
    containsIsHalfOpenAndRejectsZeroArea();
    intersectsMatchesOverlap();
    scaleKeepsTheCenterFixed();
    windowDismissalShrinksAndSettles();
    interpolationHitsBothEnds();
    remapRectRoundTripsThroughItsInverse();
    collapsedStageFillsTheSpaceTheShelfVacates();
    displayedPointRoundTripsToSourceStage();
    dragCardLiftsFromItsSlotToThePointer();
    dragLandingCentresInsideTheWorkspaceCard();
    std::cout << "OverlayGeometryTest passed\n";
    return 0;
}

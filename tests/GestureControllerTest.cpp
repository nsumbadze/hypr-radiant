#include <hypr-radiant/GestureController.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

void upwardSwipeOpensAndCommitsByDistance() {
    SwipeTracker tracker;
    tracker.begin(3, false, false, 3, 300.0);
    const auto update = tracker.update(2.0, -150.0, 100);
    assert(update.recognized);
    assert(update.justRecognized);
    assert(update.action == SwipeAction::OpenOverview);
    assert(update.progress == 0.5);
    const auto end = tracker.end(false);
    assert(end.recognized);
    assert(end.commit);
}

void downwardSwipeClosesAnActiveOverview() {
    SwipeTracker tracker;
    tracker.begin(3, true, false, 3, 300.0);
    const auto update = tracker.update(0.0, 90.0, 100);
    assert(update.recognized);
    assert(update.action == SwipeAction::CloseOverview);
    const auto end = tracker.end(false);
    assert(end.recognized);
    assert(!end.commit);
}

void horizontalAndWrongFingerGesturesPassThrough() {
    SwipeTracker tracker;
    tracker.begin(4, false, false, 3, 300.0);
    assert(!tracker.update(0.0, -200.0, 100).recognized);
    assert(!tracker.end(false).recognized);

    tracker.begin(3, false, false, 3, 300.0);
    assert(!tracker.update(80.0, -20.0, 100).recognized);
    assert(!tracker.end(false).recognized);
}

void cancelledGestureNeverCommits() {
    SwipeTracker tracker;
    tracker.begin(3, false, false, 3, 300.0);
    assert(tracker.update(0.0, -240.0, 100).recognized);
    const auto end = tracker.end(true);
    assert(end.recognized);
    assert(!end.commit);
}

void activeOverviewGesturesControlShelfAndWorkspaces() {
    SwipeTracker tracker;
    tracker.begin(3, true, false, 3, 300.0);
    assert(tracker.update(0.0, -90.0, 100).action == SwipeAction::RevealShelf);
    assert(tracker.end(false).commit);

    tracker.begin(3, true, true, 3, 300.0);
    assert(tracker.update(0.0, 90.0, 200).action == SwipeAction::HideShelf);

    tracker.begin(3, true, false, 3, 300.0);
    assert(tracker.update(-90.0, 0.0, 300).action == SwipeAction::NextWorkspace);
    assert(tracker.end(false).commit);
}

} // namespace

int main() {
    upwardSwipeOpensAndCommitsByDistance();
    downwardSwipeClosesAnActiveOverview();
    horizontalAndWrongFingerGesturesPassThrough();
    cancelledGestureNeverCommits();
    activeOverviewGesturesControlShelfAndWorkspaces();
    std::cout << "GestureControllerTest passed\n";
    return 0;
}

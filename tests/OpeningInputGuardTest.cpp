#include <hypr-radiant/input/OpeningInputGuard.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

void openingKeyIsSuppressedUntilRelease() {
    OpeningInputGuard guard;
    assert(!guard.keyEvent(28, true));
    guard.arm();
    assert(guard.keyEvent(28, true));
    assert(guard.keyEvent(28, false));
    assert(!guard.keyEvent(28, true));
}

void openingButtonIsSuppressedUntilRelease() {
    OpeningInputGuard guard;
    assert(!guard.buttonEvent(272, true));
    guard.arm();
    assert(guard.buttonEvent(272, false));
    assert(!guard.buttonEvent(272, true));
}

void inputPressedAfterArmIsAccepted() {
    OpeningInputGuard guard;
    guard.arm();
    assert(!guard.keyEvent(15, true));
    assert(!guard.buttonEvent(272, true));
}

void dispatcherArmWaitsForARealRelease() {
    OpeningInputGuard guard;
    guard.arm();
    assert(!guard.openingReleaseObserved());
    assert(!guard.keyEvent(28, true));
    assert(!guard.openingReleaseObserved());
    assert(!guard.keyEvent(28, false));
    assert(guard.openingReleaseObserved());
}

void gestureArmDoesNotWaitForKeyboardRelease() {
    OpeningInputGuard guard;
    guard.arm(false);
    assert(guard.openingReleaseObserved());
}

void explicitlySuppressedActivationKeyRequiresItsOwnRelease() {
    OpeningInputGuard guard;
    guard.arm();
    guard.suppressKeyUntilRelease(28);

    assert(guard.keyEvent(28, true));
    assert(!guard.keyEvent(42, false));
    assert(guard.keyEvent(28, true));
    assert(guard.keyEvent(28, false));
    assert(!guard.keyEvent(28, true));
}

void openingReleasesPassThroughToClearClientRepeatState() {
    assert(shouldCancelInputEvent(true, false, false));
    assert(!shouldCancelInputEvent(false, false, false));
    assert(!shouldCancelInputEvent(false, true, true));
    assert(shouldCancelInputEvent(false, true, false));
}

} // namespace

int main() {
    openingKeyIsSuppressedUntilRelease();
    openingButtonIsSuppressedUntilRelease();
    inputPressedAfterArmIsAccepted();
    dispatcherArmWaitsForARealRelease();
    gestureArmDoesNotWaitForKeyboardRelease();
    explicitlySuppressedActivationKeyRequiresItsOwnRelease();
    openingReleasesPassThroughToClearClientRepeatState();
    std::cout << "OpeningInputGuardTest passed\n";
    return 0;
}

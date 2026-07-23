#include <hypr-radiant/render/FadeAnimation.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace hypr_radiant;

namespace {

void hideImmediateResetsToInvisible() {
    auto animation = FadeAnimation{};
    animation.animateTo(true, 50);
    animation.hideImmediate();

    assert(animation.value() == 0.0);
    assert(!animation.running());
    assert(!animation.renderable());
}

void zeroDurationVisibilityJumpRendersImmediately() {
    auto animation = FadeAnimation{};
    animation.animateTo(true, 0);

    assert(animation.value() == 1.0);
    assert(!animation.running());
    assert(animation.renderable());
}

void positiveDurationVisibilityStartsRunning() {
    auto animation = FadeAnimation{};
    animation.animateTo(true, 50);

    assert(animation.running());
    assert(animation.renderable());
    assert(animation.targetVisible());
}

void visibleFadeIsMonotonic() {
    auto animation = FadeAnimation{};
    animation.hideImmediate();
    animation.animateTo(true, 60);

    std::vector<double> samples;
    samples.push_back(animation.value());

    for (int index = 0; index < 8; ++index) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        samples.push_back(animation.value());
    }

    for (std::size_t index = 1; index < samples.size(); ++index)
        assert(samples[index] + 0.0001 >= samples[index - 1]);

    assert(samples.back() <= 1.0);
    assert(animation.value() == 1.0);
    assert(!animation.running());
}

void hiddenFadeIsMonotonicAndReachesZero() {
    auto animation = FadeAnimation{};
    animation.animateTo(true, 0);
    animation.animateTo(false, 60);

    assert(animation.running());
    assert(animation.renderable());

    std::vector<double> samples;
    samples.push_back(animation.value());

    for (int index = 0; index < 8; ++index) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        samples.push_back(animation.value());
    }

    for (std::size_t index = 1; index < samples.size(); ++index)
        assert(samples[index] <= samples[index - 1] + 0.0001);

    assert(samples.back() >= 0.0);
    assert(animation.value() == 0.0);
    assert(!animation.running());
    assert(!animation.renderable());
}

} // namespace

int main() {
    hideImmediateResetsToInvisible();
    zeroDurationVisibilityJumpRendersImmediately();
    positiveDurationVisibilityStartsRunning();
    visibleFadeIsMonotonic();
    hiddenFadeIsMonotonicAndReachesZero();
    std::cout << "FadeAnimationTest passed\n";
    return 0;
}

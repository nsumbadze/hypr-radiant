#include <hypr-radiant/StageTransform.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

using namespace hypr_radiant;

namespace {

bool close(double lhs, double rhs) {
    return std::abs(lhs - rhs) < 0.0001;
}

void remapPreservesWindowAspectRatio() {
    const LayoutRect source{.x = 72.0, .y = 280.0, .width = 1776.0, .height = 720.0};
    const LayoutRect target{.x = 24.0, .y = 90.0, .width = 1872.0, .height = 910.0};
    const LayoutRect window{.x = 180.0, .y = 350.0, .width = 800.0, .height = 600.0};

    const auto mapped = remapStageRect(window, source, target);
    assert(close(mapped.width / mapped.height, window.width / window.height));
    assert(close(mapped.width / window.width, mapped.height / window.height));
}

void remapCentersUnusedTargetSpace() {
    const LayoutRect source{.x = 0.0, .y = 0.0, .width = 100.0, .height = 100.0};
    const LayoutRect target{.x = 0.0, .y = 0.0, .width = 200.0, .height = 300.0};

    const auto mapped = remapStageRect(source, source, target);
    assert(close(mapped.x, 0.0));
    assert(close(mapped.y, 50.0));
    assert(close(mapped.width, 200.0));
    assert(close(mapped.height, 200.0));
}

void pointerMappingRoundTripsRenderedGeometry() {
    const LayoutRect source{.x = 72.0, .y = 280.0, .width = 1776.0, .height = 720.0};
    const LayoutRect target{.x = 24.0, .y = 90.0, .width = 1872.0, .height = 910.0};
    const LayoutRect window{.x = 300.0, .y = 400.0, .width = 640.0, .height = 360.0};
    const auto mapped = remapStageRect(window, source, target);
    const RadiantPoint renderedCenter{.x = mapped.x + mapped.width / 2.0, .y = mapped.y + mapped.height / 2.0};

    const auto original = mapStagePointToSource(source, target, renderedCenter);
    assert(original.has_value());
    assert(close(original->x, window.x + window.width / 2.0));
    assert(close(original->y, window.y + window.height / 2.0));
}

void pointerOutsideLetterboxedContentDoesNotMap() {
    const LayoutRect source{.x = 0.0, .y = 0.0, .width = 100.0, .height = 100.0};
    const LayoutRect target{.x = 0.0, .y = 0.0, .width = 300.0, .height = 100.0};

    assert(!mapStagePointToSource(source, target, {.x = 20.0, .y = 50.0}).has_value());
}

} // namespace

int main() {
    remapPreservesWindowAspectRatio();
    remapCentersUnusedTargetSpace();
    pointerMappingRoundTripsRenderedGeometry();
    pointerOutsideLetterboxedContentDoesNotMap();
    std::cout << "StageTransformTest passed\n";
    return 0;
}

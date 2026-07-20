#include <hypr-radiant/AppIdentity.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

using namespace hypr_radiant;

namespace {

bool close(float lhs, float rhs) {
    return std::abs(lhs - rhs) < 0.0001F;
}

void glyphsAreCaseInsensitiveAndAppSpecific() {
    assert(appGlyph("Firefox") == appGlyph("firefox"));
    assert(appGlyph("Firefox") != appGlyph("Chromium"));
    assert(appGlyph("kitty") != appGlyph("Spotify"));
}

void terminalsInheritTheOmarchyAccentExactly() {
    const AppSignalColor accent{0.31F, 0.58F, 0.46F, 0.42F};
    const auto color = appSignalColor("org.wezfurlong.wezterm", accent);

    assert(close(color.r, accent.r));
    assert(close(color.g, accent.g));
    assert(close(color.b, accent.b));
    assert(close(color.a, 1.0F));
}

void knownAppsBlendTowardTheInheritedAccent() {
    const AppSignalColor accent{0.0F, 1.0F, 0.0F, 1.0F};
    const auto firefox = appSignalColor("Firefox", accent);

    assert(firefox.r < 1.0F);
    assert(firefox.g > 0.43F);
    assert(close(firefox.a, 1.0F));
}

void fallbackColorsAreStableButClassDependent() {
    const AppSignalColor accent{0.31F, 0.58F, 0.46F, 1.0F};
    const auto first = appSignalColor("com.example.alpha", accent);
    const auto repeat = appSignalColor("COM.EXAMPLE.ALPHA", accent);
    const auto second = appSignalColor("com.example.beta", accent);

    assert(close(first.r, repeat.r));
    assert(close(first.g, repeat.g));
    assert(close(first.b, repeat.b));
    assert(!close(first.r, second.r) || !close(first.g, second.g) || !close(first.b, second.b));
}

} // namespace

int main() {
    glyphsAreCaseInsensitiveAndAppSpecific();
    terminalsInheritTheOmarchyAccentExactly();
    knownAppsBlendTowardTheInheritedAccent();
    fallbackColorsAreStableButClassDependent();
    std::cout << "AppIdentityTest passed\n";
    return 0;
}

#include <hypr-radiant/overview/AppIdentity.hpp>

#include <cassert>
#include <iostream>

using namespace hypr_radiant;

namespace {

void glyphsAreCaseInsensitiveAndAppSpecific() {
    assert(appGlyph("Firefox") == appGlyph("firefox"));
    assert(appGlyph("Firefox") != appGlyph("Chromium"));
    assert(appGlyph("kitty") != appGlyph("Spotify"));
}

} // namespace

int main() {
    glyphsAreCaseInsensitiveAndAppSpecific();
    std::cout << "AppIdentityTest passed\n";
    return 0;
}

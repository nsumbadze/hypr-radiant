#include <hypr-radiant/config/Config.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

CHyprColor::CHyprColor(float, float, float, float) {}

Log::CLogger::CLogger() {}

namespace Config::Values {

IValue::IValue(Supplementary::PropRefreshBits) {}
const char* IValue::name() const {
    return "";
}
const char* IValue::description() const {
    return "";
}
Supplementary::PropRefreshBits IValue::refreshBits() const {
    return 0;
}

CFloatValue::CFloatValue(const char*, const char*, Config::FLOAT, SFloatValueOptions&&) : IValue(0) {}
const std::type_info* CFloatValue::underlying() const {
    return nullptr;
}
void CFloatValue::commence() {}
Config::FLOAT CFloatValue::value() const {
    return 0.F;
}
Config::FLOAT CFloatValue::defaultVal() const {
    return 0.F;
}

CIntValue::CIntValue(const char*, const char*, Config::INTEGER, SIntValueOptions&&) : IValue(0) {}
const std::type_info* CIntValue::underlying() const {
    return nullptr;
}
void CIntValue::commence() {}
Config::INTEGER CIntValue::value() const {
    return 0;
}
Config::INTEGER CIntValue::defaultVal() const {
    return 0;
}

CStringValue::CStringValue(const char*, const char*, Config::STRING, SStringValueOptions&&) : IValue(0) {}
const std::type_info* CStringValue::underlying() const {
    return nullptr;
}
void CStringValue::commence() {}
Config::STRING CStringValue::value() const {
    return {};
}
Config::STRING CStringValue::defaultVal() const {
    return {};
}

} // namespace Config::Values

namespace HyprlandAPI {

bool addConfigValueV2(HANDLE, SP<Config::Values::IValue>) {
    return true;
}

} // namespace HyprlandAPI

using namespace hypr_radiant;

namespace {

void parsesStageLayoutMode() {
    assert(parseLayoutMode("stage") == LayoutMode::Stage);
}

void parsesWorkspaceWallLayoutMode() {
    assert(parseLayoutMode("workspace_wall") == LayoutMode::WorkspaceWall);
}

void unknownLayoutModeFallsBackToStage() {
    assert(parseLayoutMode("unknown") == LayoutMode::Stage);
}

void emptyLayoutModeFallsBackToStage() {
    assert(parseLayoutMode("") == LayoutMode::Stage);
}

void parsesAccentFormats() {
    const auto hex = parseAccentColor("#509475");
    assert(hex.has_value());
    assert(std::abs(hex->red - 80.0F / 255.0F) < 0.001F);
    assert(std::abs(hex->green - 148.0F / 255.0F) < 0.001F);
    assert(std::abs(hex->blue - 117.0F / 255.0F) < 0.001F);
    assert(hex->alpha == 1.0F);

    const auto rgba = parseAccentColor("rgba(33ccff80)");
    assert(rgba.has_value());
    assert(std::abs(rgba->alpha - 128.0F / 255.0F) < 0.001F);

    const auto rgb = parseAccentColor("rgb(fabd47)");
    assert(rgb.has_value());
    assert(rgb->alpha == 1.0F);
}

void rejectsAutomaticAndInvalidAccents() {
    assert(!parseAccentColor("auto").has_value());
    assert(!parseAccentColor("").has_value());
    assert(!parseAccentColor("#12345").has_value());
    assert(!parseAccentColor("not-a-color").has_value());
}

void overviewGestureDefaultsAreDiscoverable() {
    assert(DEFAULT_GESTURE_ENABLED);
    assert(DEFAULT_GESTURE_FINGERS == 3);
    assert(DEFAULT_GESTURE_DISTANCE == 300.0);
}

} // namespace

int main() {
    parsesStageLayoutMode();
    parsesWorkspaceWallLayoutMode();
    unknownLayoutModeFallsBackToStage();
    emptyLayoutModeFallsBackToStage();
    parsesAccentFormats();
    rejectsAutomaticAndInvalidAccents();
    overviewGestureDefaultsAreDiscoverable();
    std::cout << "ConfigParserTest passed\n";
    return 0;
}

#include <hypr-radiant/config/Config.hpp>

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::unordered_map<const Config::Values::IValue*, std::string> configValueNames;
std::vector<std::string>                                      registeredConfigValues;
std::string                                                   rejectedConfigValue;
std::unordered_map<std::string, std::string>                    configStrings;

} // namespace

CHyprColor::CHyprColor(float, float, float, float) {}

Log::CLogger::CLogger() {}
void Log::CLogger::log(Hyprutils::CLI::eLogLevel, const std::string_view&) {}

namespace Config::Values {

IValue::IValue(Supplementary::PropRefreshBits) {}
const char* IValue::name() const {
    return configValueNames.at(this).c_str();
}
const char* IValue::description() const {
    return "";
}
Supplementary::PropRefreshBits IValue::refreshBits() const {
    return 0;
}

CFloatValue::CFloatValue(const char* name, const char*, Config::FLOAT, SFloatValueOptions&&) : IValue(0) {
    configValueNames.emplace(this, name);
}
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

CIntValue::CIntValue(const char* name, const char*, Config::INTEGER, SIntValueOptions&&) : IValue(0) {
    configValueNames.emplace(this, name);
}
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

CStringValue::CStringValue(const char* name, const char*, Config::STRING value, SStringValueOptions&&) : IValue(0) {
    configValueNames.emplace(this, name);
    configStrings[name] = value;
}
const std::type_info* CStringValue::underlying() const {
    return nullptr;
}
void CStringValue::commence() {}
Config::STRING CStringValue::value() const {
    return configStrings.at(configValueNames.at(this));
}
Config::STRING CStringValue::defaultVal() const {
    return {};
}

} // namespace Config::Values

namespace HyprlandAPI {

bool addConfigValueV2(HANDLE, SP<Config::Values::IValue> value) {
    registeredConfigValues.emplace_back(value->name());
    return registeredConfigValues.back() != rejectedConfigValue;
}

bool addNotification(HANDLE, const std::string&, const CHyprColor&, float) {
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

void parsesCarouselLayoutMode() {
    assert(parseLayoutMode("carousel") == LayoutMode::Carousel);
}

void parsesRibbonLayoutMode() {
    assert(parseLayoutMode("ribbon") == LayoutMode::Ribbon);
}

void unknownLayoutModeFallsBackToStage() {
    assert(parseLayoutMode("unknown") == LayoutMode::Stage);
}

void emptyLayoutModeFallsBackToStage() {
    assert(parseLayoutMode("") == LayoutMode::Stage);
}

void parsesShelfModes() {
    assert(parseShelfMode("always") == ShelfMode::Always);
    assert(parseShelfMode("hidden") == ShelfMode::Hidden);
    assert(parseShelfMode("unknown") == ShelfMode::Auto);
}

void parsesWindowNavigationModes() {
    assert(parseWindowNavigation("spatial") == WindowNavigation::Spatial);
    assert(parseWindowNavigation("list") == WindowNavigation::List);
    assert(parseWindowNavigation("unknown") == WindowNavigation::List);
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

void overviewShortcutDefaultIsDiscoverable() {
    assert(DEFAULT_SHORTCUT_ENABLED);
}

void unregisteredConfigUsesCustomizationDefaults() {
    RadiantConfig config;
    assert(config.chromePreset() == ChromePreset::Radiant);
    assert(config.roundingOverride() == -1);
    assert(config.borderSizeOverride() == -1);
    assert(!config.borderColorOverride());
    assert(config.effectsMode() == EffectsMode::Auto);
    assert(config.spacing() == 1.0);
    assert(config.shelfMode() == ShelfMode::Auto);
    assert(!config.allShelfTargets());
    assert(config.windowNavigation() == WindowNavigation::List);
    assert(!config.vimKeys());
    assert(!config.tabCyclesWindows());
}

void registersEveryPluginOptionBeforeRuntimeSetup() {
    registeredConfigValues.clear();
    rejectedConfigValue.clear();

    RadiantConfig config;
    assert(config.registerValues(nullptr));

    const std::vector<std::string> expected{
        "plugin:radiant:opacity",
        "plugin:radiant:animation_duration",
        "plugin:radiant:layout",
        "plugin:radiant:background_color",
        "plugin:radiant:foreground_color",
        "plugin:radiant:font_family",
        "plugin:radiant:gesture_enabled",
        "plugin:radiant:gesture_fingers",
        "plugin:radiant:gesture_distance",
        "plugin:radiant:shortcut_enabled",
        "plugin:radiant:chrome",
        "plugin:radiant:rounding",
        "plugin:radiant:border_size",
        "plugin:radiant:border_color",
        "plugin:radiant:effects",
        "plugin:radiant:spacing",
        "plugin:radiant:shelf",
        "plugin:radiant:window_navigation",
        "plugin:radiant:shelf_navigation",
        "plugin:radiant:vim_keys",
        "plugin:radiant:tab_cycles_windows",
    };
    assert(registeredConfigValues == expected);
    assert(config.registrationError().empty());
    assert(!config.allShelfTargets());
    configStrings["plugin:radiant:shelf_navigation"] = "all";
    assert(config.allShelfTargets());
    configStrings["plugin:radiant:shelf_navigation"] = "unknown";
    assert(!config.allShelfTargets());
}

void registrationFailureNamesTheRejectedOption() {
    registeredConfigValues.clear();
    rejectedConfigValue = "plugin:radiant:gesture_distance";

    RadiantConfig config;
    assert(!config.registerValues(nullptr));
    assert(config.registrationError().contains(rejectedConfigValue));
    assert(config.registrationError().contains("rebuild"));

    rejectedConfigValue.clear();
}

} // namespace

int main() {
    parsesStageLayoutMode();
    parsesWorkspaceWallLayoutMode();
    parsesCarouselLayoutMode();
    parsesRibbonLayoutMode();
    unknownLayoutModeFallsBackToStage();
    emptyLayoutModeFallsBackToStage();
    parsesShelfModes();
    parsesWindowNavigationModes();
    parsesAccentFormats();
    rejectsAutomaticAndInvalidAccents();
    overviewGestureDefaultsAreDiscoverable();
    overviewShortcutDefaultIsDiscoverable();
    unregisteredConfigUsesCustomizationDefaults();
    registersEveryPluginOptionBeforeRuntimeSetup();
    registrationFailureNamesTheRejectedOption();
    std::cout << "ConfigParserTest passed\n";
    return 0;
}

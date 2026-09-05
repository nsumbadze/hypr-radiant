#include <hypr-radiant/config/Preferences.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace hypr_radiant {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

std::string themeSlug(std::string_view value) {
    if (value == "auto")
        return {};
    if (value.empty() || value.front() == '.' || !std::ranges::all_of(value, [](unsigned char character) {
            return std::islower(character) || std::isdigit(character) || character == '-' || character == '_' || character == '.';
       }))
        return {};
    return std::string{value};
}

void parseLine(PreferencesState& preferences, std::string_view line) {
    line = trim(line);
    if (line.empty() || line.starts_with('#'))
        return;

    const auto separator = line.find('=');
    if (separator == std::string_view::npos)
        return;

    const auto key   = trim(line.substr(0, separator));
    const auto value = trim(line.substr(separator + 1));
    if (key == "workspace_view") {
        if (value == "stage")
            preferences.workspaceView = WorkspaceViewPreference::Stage;
        else if (value == "workspace_wall")
            preferences.workspaceView = WorkspaceViewPreference::WorkspaceWall;
        else if (value == "carousel")
            preferences.workspaceView = WorkspaceViewPreference::Carousel;
        else if (value == "ribbon")
            preferences.workspaceView = WorkspaceViewPreference::Ribbon;
        else
            preferences.workspaceView = WorkspaceViewPreference::FollowConfig;
    } else if (key == "window_view") {
        if (value == "grouped")
            preferences.windowView = WindowViewPreference::Grouped;
        else if (value == "deck")
            preferences.windowView = WindowViewPreference::Deck;
        else
            preferences.windowView = WindowViewPreference::Spatial;
    } else if (key == "motion") {
        if (value == "snap" || value == "quattro")
            preferences.motion = MotionPreference::Quattro;
        else if (value == "glitch" || value == "cyberpunk")
            preferences.motion = MotionPreference::Cyberpunk;
        else if (value == "lightcycle" || value == "tron")
            preferences.motion = MotionPreference::Tron;
        else if (value == "silk" || value == "elegant")
            preferences.motion = MotionPreference::Elegant;
        else if (value == "reduced")
            preferences.motion = MotionPreference::Reduced;
        else if (value == "off")
            preferences.motion = MotionPreference::Off;
        else
            preferences.motion = MotionPreference::FollowConfig;
    } else if (key == "chrome") {
        if (value == "radiant")
            preferences.chrome = ChromePreference::Radiant;
        else if (value == "native")
            preferences.chrome = ChromePreference::Native;
        else if (value == "flat")
            preferences.chrome = ChromePreference::Flat;
        else
            preferences.chrome = ChromePreference::FollowConfig;
    } else if (key == "shelf") {
        if (value == "auto")
            preferences.shelf = ShelfPreference::Auto;
        else if (value == "always")
            preferences.shelf = ShelfPreference::Always;
        else if (value == "hidden")
            preferences.shelf = ShelfPreference::Hidden;
        else
            preferences.shelf = ShelfPreference::FollowConfig;
    } else if (key == "window_navigation") {
        preferences.windowNavigation = value == "list" ? WindowNavigationPreference::List
            : value == "spatial" ? WindowNavigationPreference::Spatial : WindowNavigationPreference::FollowConfig;
    } else if (key == "native_theme") {
        preferences.nativeTheme = themeSlug(value);
    }
}

std::string_view value(WorkspaceViewPreference preference) {
    switch (preference) {
    case WorkspaceViewPreference::Stage:
        return "stage";
    case WorkspaceViewPreference::WorkspaceWall:
        return "workspace_wall";
    case WorkspaceViewPreference::Carousel:
        return "carousel";
    case WorkspaceViewPreference::Ribbon:
        return "ribbon";
    case WorkspaceViewPreference::FollowConfig:
        return "config";
    }
    return "config";
}

std::string_view value(WindowViewPreference preference) {
    switch (preference) {
    case WindowViewPreference::Grouped:
        return "grouped";
    case WindowViewPreference::Deck:
        return "deck";
    case WindowViewPreference::Spatial:
        return "spatial";
    }
    return "spatial";
}

std::string_view value(MotionPreference preference) {
    switch (preference) {
    case MotionPreference::Quattro:
        return "snap";
    case MotionPreference::Cyberpunk:
        return "glitch";
    case MotionPreference::Tron:
        return "lightcycle";
    case MotionPreference::Elegant:
        return "silk";
    case MotionPreference::Reduced:
        return "reduced";
    case MotionPreference::Off:
        return "off";
    case MotionPreference::FollowConfig:
        return "config";
    }
    return "config";
}

std::string_view value(ChromePreference preference) {
    switch (preference) {
    case ChromePreference::Radiant: return "radiant";
    case ChromePreference::Native: return "native";
    case ChromePreference::Flat: return "flat";
    case ChromePreference::FollowConfig: return "config";
    }
    return "config";
}

std::string_view value(WindowNavigationPreference preference) {
    switch (preference) {
    case WindowNavigationPreference::List: return "list";
    case WindowNavigationPreference::Spatial: return "spatial";
    case WindowNavigationPreference::FollowConfig: return "config";
    }
    return "config";
}

std::string_view value(ShelfPreference preference) {
    switch (preference) {
    case ShelfPreference::Auto: return "auto";
    case ShelfPreference::Always: return "always";
    case ShelfPreference::Hidden: return "hidden";
    case ShelfPreference::FollowConfig: return "config";
    }
    return "config";
}

} // namespace

PreferencesState parsePreferences(std::string_view contents) {
    PreferencesState preferences;
    while (!contents.empty()) {
        const auto lineEnd = contents.find('\n');
        parseLine(preferences, contents.substr(0, lineEnd));
        if (lineEnd == std::string_view::npos)
            break;
        contents.remove_prefix(lineEnd + 1);
    }
    return preferences;
}

std::string serializePreferences(const PreferencesState& preferences) {
    return "# hypr-radiant preferences\n"
        "workspace_view = " + std::string{value(preferences.workspaceView)} + "\n"
        "window_view = " + std::string{value(preferences.windowView)} + "\n"
        "motion = " + std::string{value(preferences.motion)} + "\n"
        "chrome = " + std::string{value(preferences.chrome)} + "\n"
        "shelf = " + std::string{value(preferences.shelf)} + "\n"
        "window_navigation = " + std::string{value(preferences.windowNavigation)} + "\n"
        "native_theme = " + (preferences.nativeTheme.empty() ? "auto" : preferences.nativeTheme) + "\n";
}

std::filesystem::path defaultPreferencesPath() {
    if (const auto* configHome = std::getenv("XDG_CONFIG_HOME"); configHome && *configHome)
        return std::filesystem::path{configHome} / "hypr-radiant" / "preferences.conf";
    if (const auto* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path{home} / ".config" / "hypr-radiant" / "preferences.conf";
    return "preferences.conf";
}

PreferencesStore::PreferencesStore(std::filesystem::path path) : m_path(std::move(path)) {}

void PreferencesStore::load() {
    std::ifstream input{m_path};
    if (!input)
        return;

    const std::string contents{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    m_state = parsePreferences(contents);
}

bool PreferencesStore::save() const {
    std::error_code error;
    if (const auto parent = m_path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error)
            return false;
    }

    const auto temporary = m_path.string() + ".tmp";
    {
        std::ofstream output{temporary, std::ios::trunc};
        if (!output)
            return false;
        output << serializePreferences(m_state);
        if (!output)
            return false;
    }

    std::filesystem::rename(temporary, m_path, error);
    if (!error)
        return true;

    std::filesystem::remove(m_path, error);
    error.clear();
    std::filesystem::rename(temporary, m_path, error);
    return !error;
}

const PreferencesState& PreferencesStore::state() const noexcept {
    return m_state;
}

PreferencesState& PreferencesStore::state() noexcept {
    return m_state;
}

const std::filesystem::path& PreferencesStore::path() const noexcept {
    return m_path;
}

std::string_view label(WorkspaceViewPreference preference) {
    switch (preference) {
    case WorkspaceViewPreference::Stage:
        return "STAGE";
    case WorkspaceViewPreference::WorkspaceWall:
        return "WALL";
    case WorkspaceViewPreference::Carousel:
        return "CAROUSEL";
    case WorkspaceViewPreference::Ribbon:
        return "RIBBON";
    case WorkspaceViewPreference::FollowConfig:
        return "CONFIG";
    }
    return "CONFIG";
}

std::string_view label(WindowViewPreference preference) {
    switch (preference) {
    case WindowViewPreference::Grouped:
        return "GROUPED";
    case WindowViewPreference::Deck:
        return "DECK";
    case WindowViewPreference::Spatial:
        return "SPATIAL";
    }
    return "SPATIAL";
}

std::string_view label(MotionPreference preference) {
    switch (preference) {
    case MotionPreference::Quattro:
        return "SNAP";
    case MotionPreference::Cyberpunk:
        return "GLITCH";
    case MotionPreference::Tron:
        return "LIGHTCYCLE";
    case MotionPreference::Elegant:
        return "SILK";
    case MotionPreference::Reduced:
        return "REDUCED";
    case MotionPreference::Off:
        return "OFF";
    case MotionPreference::FollowConfig:
        return "DEFAULT";
    }
    return "DEFAULT";
}

std::string_view label(ChromePreference preference) {
    switch (preference) {
    case ChromePreference::Radiant: return "RADIANT";
    case ChromePreference::Native: return "NATIVE";
    case ChromePreference::Flat: return "FLAT";
    case ChromePreference::FollowConfig: return "CONFIG";
    }
    return "CONFIG";
}

std::string_view label(WindowNavigationPreference preference) {
    switch (preference) {
    case WindowNavigationPreference::List: return "LIST";
    case WindowNavigationPreference::Spatial: return "SPATIAL";
    case WindowNavigationPreference::FollowConfig: return "CONFIG";
    }
    return "CONFIG";
}

std::string_view preferenceSourceLabel(bool followsConfig, bool nativeUnavailable) {
    if (nativeUnavailable)
        return followsConfig ? "Config: Native unavailable; using Flat" : "Saved override: Native unavailable; using Flat";
    return followsConfig ? "Follows Hyprland config" : "Saved override; choose Config to follow Hyprland";
}

std::string_view label(ShelfPreference preference) {
    switch (preference) {
    case ShelfPreference::Auto: return "AUTO";
    case ShelfPreference::Always: return "ALWAYS";
    case ShelfPreference::Hidden: return "HIDDEN";
    case ShelfPreference::FollowConfig: return "CONFIG";
    }
    return "CONFIG";
}

} // namespace hypr_radiant

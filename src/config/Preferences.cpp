#include <hypr-radiant/config/Preferences.hpp>

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
        else
            preferences.workspaceView = WorkspaceViewPreference::FollowConfig;
    } else if (key == "window_view") {
        preferences.windowView = value == "grouped" ? WindowViewPreference::Grouped : WindowViewPreference::Spatial;
    } else if (key == "accent") {
        if (value == "green")
            preferences.accent = AccentPreference::Green;
        else if (value == "blue")
            preferences.accent = AccentPreference::Blue;
        else if (value == "violet")
            preferences.accent = AccentPreference::Violet;
        else
            preferences.accent = AccentPreference::FollowConfig;
    } else if (key == "motion") {
        if (value == "reduced")
            preferences.motion = MotionPreference::Reduced;
        else if (value == "off")
            preferences.motion = MotionPreference::Off;
        else
            preferences.motion = MotionPreference::FollowConfig;
    }
}

std::string_view value(WorkspaceViewPreference preference) {
    switch (preference) {
    case WorkspaceViewPreference::Stage:
        return "stage";
    case WorkspaceViewPreference::WorkspaceWall:
        return "workspace_wall";
    case WorkspaceViewPreference::FollowConfig:
        return "config";
    }
    return "config";
}

std::string_view value(WindowViewPreference preference) {
    return preference == WindowViewPreference::Grouped ? "grouped" : "spatial";
}

std::string_view value(AccentPreference preference) {
    switch (preference) {
    case AccentPreference::Green:
        return "green";
    case AccentPreference::Blue:
        return "blue";
    case AccentPreference::Violet:
        return "violet";
    case AccentPreference::FollowConfig:
        return "config";
    }
    return "config";
}

std::string_view value(MotionPreference preference) {
    switch (preference) {
    case MotionPreference::Reduced:
        return "reduced";
    case MotionPreference::Off:
        return "off";
    case MotionPreference::FollowConfig:
        return "config";
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
        "accent = " + std::string{value(preferences.accent)} + "\n"
        "motion = " + std::string{value(preferences.motion)} + "\n";
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
    case WorkspaceViewPreference::FollowConfig:
        return "CONFIG";
    }
    return "CONFIG";
}

std::string_view label(WindowViewPreference preference) {
    return preference == WindowViewPreference::Grouped ? "GROUPED" : "SPATIAL";
}

std::string_view label(AccentPreference preference) {
    switch (preference) {
    case AccentPreference::Green:
        return "GREEN";
    case AccentPreference::Blue:
        return "BLUE";
    case AccentPreference::Violet:
        return "VIOLET";
    case AccentPreference::FollowConfig:
        return "THEME";
    }
    return "THEME";
}

std::string_view label(MotionPreference preference) {
    switch (preference) {
    case MotionPreference::Reduced:
        return "REDUCED";
    case MotionPreference::Off:
        return "OFF";
    case MotionPreference::FollowConfig:
        return "DEFAULT";
    }
    return "DEFAULT";
}

} // namespace hypr_radiant
